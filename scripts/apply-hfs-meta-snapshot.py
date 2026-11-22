#!/usr/bin/env python3
"""Build a sparse HFSX image containing the live metadata forks only."""

import argparse
import json
import os
import pathlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--metadata-dir", required=True, type=pathlib.Path)
    parser.add_argument("--size", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as output:
        output.truncate(args.size)
        for item in manifest:
            logical = item["logical"]
            path = args.metadata_dir / item["file"]
            data = path.read_bytes()
            if len(data) != item["bytes"]:
                raise SystemExit(f"{path}: size mismatch")
            output.seek(logical)
            output.write(data)
    os.truncate(args.out, args.size)
    print(f"snapshot={args.out} sparse_size={args.size} chunks={len(manifest)}")


if __name__ == "__main__":
    main()
