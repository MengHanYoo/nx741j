#!/usr/bin/env python3
"""Record the exact inputs and hashes of a freshly built NX741J iOS root."""
import argparse
import hashlib
import json
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def describe(path):
    path = Path(path)
    return {"path": str(path.resolve()), "bytes": path.stat().st_size, "sha256": sha256(path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--ipsw")
    parser.add_argument("--os", required=True)
    parser.add_argument("--systemos", required=True)
    parser.add_argument("--appos", required=True)
    parser.add_argument("--live-dtb", required=True)
    parser.add_argument("--d22ap", required=True)
    parser.add_argument("--extents", required=True)
    args = parser.parse_args()

    image = Path(args.image)
    record = {
        "schema": 1,
        "target": {
            "device": "NX741J",
            "soc": "SM8850",
            "ios": "16.7.16",
            "build": "20H392",
            "userland": "iPhone10,3 / D22AP",
            "filesystem": "case-sensitive journaled HFS+ (HFSX)",
            "size_bytes": image.stat().st_size,
        },
        "source": {
            "golden_image_used": False,
            **({"ipsw": describe(args.ipsw)} if args.ipsw else {}),
            "os": describe(args.os),
            "systemos": describe(args.systemos),
            "appos": describe(args.appos),
        },
        "platform_evidence": {
            "live_dtb": describe(args.live_dtb),
            "d22ap_adt": describe(args.d22ap),
            "extent_table": {
                **describe(args.extents),
                "data": json.loads(Path(args.extents).read_text()),
            },
        },
        "procedure": [
            "ios-fuxi/tools/mkiosroot.sh from the three original IPSW APFS images",
            "ios-fuxi/tools/fuxi-stage-root.py",
            "ios-fuxi/tools/sweet-root-tweaks.py",
            "fsck.hfsplus -f -y then -f -n",
            "mount and verify /sbin/launchd and 20H392 SystemVersion",
        ],
        "image": describe(image),
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n")
    print(f"manifest: {output}")


if __name__ == "__main__":
    main()
