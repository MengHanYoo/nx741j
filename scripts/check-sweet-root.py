#!/usr/bin/env python3
"""Gate the data-a-live extent-root path end to end on the host.

Checks the live-FDT-derived payload ADT, pinfile extent table, linked kernel
symbols, and (when supplied) the freshly built HFSX image. This is separate
from the legacy ShengUfs/VerifiedRoot gate.
"""
import argparse
import importlib.util
import json
import struct
import subprocess
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_adt_parser():
    path = ROOT / "scripts/postprocess-ios-adt.py"
    spec = importlib.util.spec_from_file_location("ios_adt", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract_adt(payload):
    handle = tempfile.NamedTemporaryFile(suffix=".adt", delete=False)
    handle.close()
    target = Path(handle.name)
    target.unlink()
    subprocess.run([
        "mcopy", "-o", "-i", str(payload),
        "::/EFI/UniXNU/Payloads/kernel.macho.adt", str(target),
    ], check=True)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adt")
    parser.add_argument("--payload", type=Path)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--extents", type=Path, required=True)
    parser.add_argument("--root-image", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--device", type=Path, default=ROOT)
    args = parser.parse_args()

    temporary = None
    if args.adt:
        adt_path = Path(args.adt)
    elif args.payload:
        temporary = extract_adt(args.payload)
        adt_path = temporary
    else:
        parser.error("pass --adt or --payload")

    adt_tool = load_adt_parser()
    adt_blob = adt_path.read_bytes()
    root, used = adt_tool.parse(adt_blob)
    if used != len(adt_blob):
        raise SystemExit("ADT has trailing bytes")
    chosen = root.child("chosen")
    product = root.child("product")
    table = json.loads(args.extents.read_text())
    board = tomllib.loads((args.device / "board.toml").read_text())
    userdata = board["userdata"]

    checks = []

    def check(name, condition, detail=""):
        checks.append((name, bool(condition), detail))

    compatible = (root.prop("compatible") or b"").split(b"\0")
    check("root identity begins D22AP / iPhone10,3",
          compatible[:2] == [b"D22AP", b"iPhone10,3"],
          " / ".join(value.decode(errors="replace") for value in compatible[:3]))
    check("root model is iPhone10,3", root.prop("model") == b"iPhone10,3\0",
          repr(root.prop("model")))
    check("target is D22AP", root.prop("target-sub-type") == b"D22AP\0",
          repr(root.prop("target-sub-type")))
    check("/product exists", product is not None)
    if product is not None:
        check("/product is iPhone X", product.prop("product-name") == b"iPhone X\0",
              repr(product.prop("product-name")))
        expected_id = bytes.fromhex("fd079b298144a7de b4d7c87fef3eb474 d8f0e7eb")
        check("/product-id is the D22AP value", product.prop("product-id") == expected_id,
              (product.prop("product-id") or b"").hex())

    check("old APFS root-matching is absent",
          chosen is not None and chosen.prop("root-matching") is None)
    extents_prop = chosen.prop("fuxi-root-extents") if chosen else None
    blocks_prop = chosen.prop("fuxi-root-blocks") if chosen else None
    source_extents = table["extents"]
    expected_words = []
    logical = 0
    physical = []
    for entry in source_extents:
        file_off, part_off, length, _flags = (int(value) for value in entry)
        if file_off != logical:
            raise SystemExit("pinfile extent table is not contiguous")
        expected_words += [file_off // 4096, part_off // 4096, length // 4096]
        physical.append((part_off, part_off + length))
        logical += length
    expected_blob = struct.pack(f"<{len(expected_words)}Q", *expected_words)
    check("fuxi-root-extents matches pinfile JSON",
          extents_prop == expected_blob,
          f"{len(extents_prop or b'') // 24} extents")
    check("fuxi-root-blocks is 16 GiB",
          blocks_prop == struct.pack("<Q", table["size"] // 4096),
          f"{table['size']} bytes")
    physical.sort()
    check("physical extents do not overlap",
          all(a[1] <= b[0] for a, b in zip(physical, physical[1:])))
    partition_bytes = (userdata["last_lba"] - userdata["first_lba"] + 1) * userdata["lba_bytes"]
    check("physical extents stay in userdata",
          physical and physical[0][0] >= 0 and physical[-1][1] <= partition_bytes,
          f"{physical[0][0] if physical else 0:#x}..{physical[-1][1] if physical else 0:#x}")
    phandles = adt_tool.check_phandles(root)
    check("ADT phandles are unique", len(phandles) > 0, f"{len(phandles)} handles")

    symbols = subprocess.run(
        ["llvm-nm", "--defined-only", str(args.kernel)],
        check=True, capture_output=True, text=True).stdout
    required = {
        "_sweet_ufs_rootdev": "ufsroot0 root device",
        "_fx_load_extents": "extent parser",
        "_ufs_hibernate_exit": "SM8850 UniPro wake",
        "_ufs_device_active": "WLUN ACTIVE power command",
        "_sheng_pmgr_run": "prelinked AMFI PMGR binding",
    }
    for symbol, description in required.items():
        check(f"kernel links {description}",
              any(line.split() and line.split()[-1] == symbol for line in symbols.splitlines()),
              symbol)
    kernel_blob = args.kernel.read_bytes()
    check("kernel links Apple HFS kext 627.100.6",
          b"com.apple.filesystems.hfs.kext" in kernel_blob and
          b"627.100.6" in kernel_blob and b"hfs_mounthfsplus" in kernel_blob,
          "same HFS generation as data-a-live")
    check("kernel text carries unsealed-root exception",
          b"continuing unsealed" in kernel_blob, "printf string")

    if args.root_image:
        with open(args.root_image, "rb") as stream:
            stream.seek(1024)
            signature = stream.read(2)
        check("root image is HFSX",
              args.root_image.stat().st_size == table["size"] and signature == b"HX",
              f"{args.root_image.stat().st_size} bytes, {signature!r}")
    if args.manifest:
        record = json.loads(args.manifest.read_text())
        check("manifest records no golden-image input",
              record.get("source", {}).get("golden_image_used") is False)
        image = record.get("image", {})
        check("manifest image matches the root extent file",
              image.get("bytes") == table["size"], str(image.get("sha256")))

    width = max(len(name) for name, _ok, _detail in checks)
    failed = 0
    for name, ok, detail in checks:
        failed += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"\n{len(checks)} checks, {failed} failed")
    if temporary:
        temporary.unlink(missing_ok=True)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
