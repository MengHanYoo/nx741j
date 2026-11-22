#!/usr/bin/env python3
"""Stage the previously proven NX741J Metal bundle and firmware pack."""

import argparse
import hashlib
import os
import pathlib
import shutil


BUNDLE = "System/Library/Extensions/NativeAdrenoMetal.bundle"
FW = "private/var/fuxi/iosfw.img"
PLUGIN_SHA256 = "04ddea9f0415db9a2fae067521482aa14a938931c1a576cfcb797f7e48a56edf"
INFO_SHA256 = "52c9659d7d1da9a46aaed8030634cf466d87e89f0a405eeb9bd8925906b4beae"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument(
        "--plugin-dir",
        type=pathlib.Path,
        default=pathlib.Path("/opt/work/gpu/metal-out/plat"),
    )
    parser.add_argument(
        "--fwpack",
        type=pathlib.Path,
        default=pathlib.Path(
            "/mnt/e/default-workspace/z80u-collect/fwpack.img"
        ),
    )
    parser.add_argument("--fw-bytes", type=lambda value: int(value, 0), default=0x200000)
    args = parser.parse_args()

    root = args.root.resolve()
    if os.geteuid() != 0 or root == pathlib.Path("/"):
        parser.error("run as root against a mounted image, never /")
    if not (root / "sbin/launchd").is_file():
        parser.error("not a staged iOS root")
    binary = args.plugin_dir / "NativeAdrenoMetal"
    info = args.plugin_dir / "Info.plist"
    if not info.is_file():
        info = args.plugin_dir.parent / "NativeAdrenoMetal.bundle/Info.plist"
    if not info.is_file():
        info = args.plugin_dir.parent / "plat2/Info.plist"
    fwpack = args.fwpack
    for path in (binary, info, fwpack):
        if not path.is_file():
            parser.error(f"missing input: {path}")
    if hashlib.sha256(binary.read_bytes()).hexdigest() != PLUGIN_SHA256:
        parser.error("plugin does not match the proven lockscreen build")
    if hashlib.sha256(info.read_bytes()).hexdigest() != INFO_SHA256:
        parser.error("plugin Info.plist does not match the proven lockscreen build")
    if fwpack.read_bytes()[:8] != b"PANDFW01":
        parser.error("firmware pack does not begin with PANDFW01")
    if args.fw_bytes % 4096:
        parser.error("firmware file size must be 4 KiB aligned")

    bundle = root / BUNDLE
    bundle.mkdir(parents=True, exist_ok=True)
    for path in (bundle, root / "System/Library/Extensions"):
        os.chown(path, 0, 0)
        os.chmod(path, 0o755)
    for source, name, mode in (
        (binary, "NativeAdrenoMetal", 0o755),
        (info, "Info.plist", 0o644),
    ):
        target = bundle / name
        shutil.copyfile(source, target)
        os.chown(target, 0, 0)
        os.chmod(target, mode)

    fw = root / FW
    fw.parent.mkdir(parents=True, exist_ok=True)
    os.chown(fw.parent, 0, 0)
    os.chmod(fw.parent, 0o755)
    payload = fwpack.read_bytes()
    if len(payload) > args.fw_bytes:
        parser.error("firmware pack exceeds the target file size")
    with fw.open("wb") as output:
        output.write(payload)
        output.write(bytes(args.fw_bytes - len(payload)))
    os.chown(fw, 0, 0)
    os.chmod(fw, 0o644)
    print(f"staged {BUNDLE}: {binary} + Info.plist")
    print(f"staged {FW}: {args.fw_bytes} bytes")


if __name__ == "__main__":
    main()
