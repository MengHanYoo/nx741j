#!/usr/bin/env python3
"""Static gate for the NX741J shim-loader -> SWEETXNU -> XNU route."""

import argparse
import hashlib
import json
import pathlib
import struct
import sys


PAGE = 4096
PAYLOAD_FILE_OFFSET = 0xC000
SEG_TABLE = 0x2A8


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb", buffering=0) as source:
        while True:
            block = source.read(16 * 1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", required=True, type=pathlib.Path)
    parser.add_argument("--root", required=True, type=pathlib.Path)
    parser.add_argument("--extents", required=True, type=pathlib.Path)
    parser.add_argument("--adt-tools", required=True, type=pathlib.Path)
    parser.add_argument("--json-out", type=pathlib.Path)
    args = parser.parse_args()

    sys.path.insert(0, str(args.adt_tools))
    import adt  # noqa: E402

    boot = args.boot.read_bytes()
    if boot[:8] != b"ANDROID!":
        fail("boot image magic")
    kernel_size, ramdisk_size, os_version, header_size = struct.unpack_from(
        "<IIII", boot, 8
    )
    header_version, = struct.unpack_from("<I", boot, 40)
    if header_version != 4 or header_size != 1584:
        fail(f"boot image v4/1584 required, got v{header_version}/{header_size}")
    if ramdisk_size:
        fail(f"loader boot image must have no ramdisk, got {ramdisk_size}")
    if os_version != 0:
        fail(f"unexpected boot image os_version {os_version:#x}")
    kernel_offset = (header_size + PAGE - 1) & ~(PAGE - 1)
    image = boot[kernel_offset : kernel_offset + kernel_size]
    if len(image) != kernel_size:
        fail("truncated loader Image")
    if image[0x38:0x3C] != b"ARM\x64":
        fail("loader is not an arm64 Image")
    image_size, = struct.unpack_from("<Q", image, 16)
    if image_size != len(image):
        fail(f"arm64 Image image_size {image_size:#x} != {len(image):#x}")
    if len(boot) > 100663296:
        fail("boot image exceeds the NX741J boot_a partition")

    payload = image[PAYLOAD_FILE_OFFSET:]
    magic, version, nsegs = struct.unpack_from("<8sII", payload)
    if magic != b"SWEETXNU" or version != 1:
        fail(f"SWEETXNU header {magic!r} v{version}")
    if not 0 < nsegs <= 64:
        fail(f"invalid segment count {nsegs}")
    (
        virt_base,
        entry,
        adt_off,
        adt_size,
        kernel_end,
        phys_base,
        mem_size,
    ) = struct.unpack_from("<7Q", payload, 0x10)
    if adt_off % 8 or adt_off + adt_size > len(payload):
        fail("payload ADT range")
    adt_blob = payload[adt_off : adt_off + adt_size]
    segs = [
        struct.unpack_from("<4Q", payload, SEG_TABLE + 32 * i)
        for i in range(nsegs)
    ]
    for va, vmsize, file_off, file_size in segs:
        if file_size > vmsize or file_off + file_size > len(payload):
            fail(f"unsafe segment {va:#x}")
    if not any(va <= entry < va + vmsize for va, vmsize, _, _ in segs):
        fail("entry point is outside payload segments")

    cmdline = payload[0x48 : 0x48 + 608].split(b"\0", 1)[0].decode()
    required_args = {
        "serial=3",
        "cpus=8",
        "rd=ufsroot0",
        "ufsrw=1",
        "rootrw=1",
        "unentitled_ios_sim_launch=1",
        "ignition_level=1",
        "launchd_unsecure_cache=1",
        "wdt=-1",
        "sweetfb_fill=1",
    }
    words = set(cmdline.split())
    missing = required_args - words
    if missing:
        fail(f"missing boot args: {sorted(missing)}")
    for forbidden in ("rd=md0", "native-ufs-noverify=1", "nxroot-lba="):
        if any(word.startswith(forbidden) for word in words):
            fail(f"stale boot arg {forbidden}")

    root, end = adt.parse(adt_blob)
    if end != len(adt_blob):
        fail("ADT trailing bytes")
    chosen = next(c for c in root.children if c.name == "chosen")
    root_table = chosen.prop("fuxi-root-extents")
    root_blocks = chosen.prop("fuxi-root-blocks")
    if root_table is None or root_blocks is None:
        fail("ADT has no fuxi root extent mapping")
    extent_json = json.loads(args.extents.read_text())
    expected = b"".join(
        struct.pack(
            "<3Q",
            logical // PAGE,
            physical // PAGE,
            length // PAGE,
        )
        for logical, physical, length, _flags in extent_json["extents"]
    )
    if root_table != expected:
        fail("ADT root extent table does not match pinfile JSON")
    if struct.unpack("<Q", root_blocks)[0] != extent_json["size"] // PAGE:
        fail("ADT root block count")
    if extent_json["size"] != 16 * 1024**3:
        fail("root is not 16 GiB")
    for stale in (
        "pandora-log-extents",
        "pandora-log-blocks",
        "pandora-fw-extents",
        "pandora-fw-blocks",
    ):
        if chosen.prop(stale) is not None:
            fail(f"stale physical mapping in ADT: {stale}")
    triples = [
        struct.unpack_from("<3Q", root_table, i * 24)
        for i in range(len(root_table) // 24)
    ]
    if min(x[1] * PAGE for x in triples) < 0:
        fail("negative physical extent")
    if max((x[1] + x[2]) * PAGE for x in triples) >= 1 << 36:
        fail("root extent exceeds the NX741J 36-bit DMA window")

    trust = chosen.children
    mmap = next(c for c in trust if c.name == "memory-map")
    tc_range = mmap.prop("TrustCache")
    if tc_range is None or len(tc_range) != 16:
        fail("ADT TrustCache memory-map range")
    tc_pa, tc_size = struct.unpack("<2Q", tc_range)
    if not phys_base <= tc_pa < phys_base + mem_size:
        fail("TrustCache PA outside kernel RAM")
    tc_segments = [
        (va, size, off, n)
        for va, size, off, n in segs
        if va <= tc_pa - phys_base + virt_base < va + size
    ]
    if len(tc_segments) != 1:
        fail("trust cache is not in exactly one payload segment")
    tc_va, tc_vmsize, tc_off, tc_filesize = tc_segments[0]
    tc_blob = payload[tc_off : tc_off + tc_filesize]
    if tc_size > len(tc_blob):
        fail("ADT trust cache length exceeds segment")
    if tc_size < 8:
        fail("trust cache bundle too short")
    modules, = struct.unpack_from("<I", tc_blob)
    if modules != 2:
        fail(f"expected 2 trust cache modules, got {modules}")
    offsets = struct.unpack_from(f"<{modules}I", tc_blob, 4)
    module_entries = []
    for off in offsets:
        version, entries = struct.unpack_from("<I16xI", tc_blob, off)
        if version != 2 or 24 + 24 * entries > len(tc_blob) - off:
            fail("invalid trust cache module")
        module_entries.append(entries)

    root_digest = file_sha256(args.root)
    report = {
        "schema": 1,
        "boot": str(args.boot),
        "boot_sha256": sha256(boot),
        "loader_image_bytes": len(image),
        "payload_bytes": len(payload),
        "payload_sha256": sha256(payload),
        "payload_segments": nsegs,
        "cmdline": cmdline,
        "adt_bytes": len(adt_blob),
        "adt_sha256": sha256(adt_blob),
        "root_extents": len(triples),
        "root_blocks": struct.unpack("<Q", root_blocks)[0],
        "trustcache_modules": modules,
        "trustcache_entries": module_entries,
        "trustcache_phys": tc_pa,
        "phys_base": phys_base,
        "mem_size": mem_size,
        "entry": entry,
        "root_sha256": root_digest,
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2) + "\n")
    for key, value in report.items():
        print(f"{key}: {value}")
    print("PASS: proven-route static gate")


if __name__ == "__main__":
    main()
