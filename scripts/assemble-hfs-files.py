#!/usr/bin/env python3
"""Assemble HFS+ file extents read through read-hfs-files-9008.ps1."""

import argparse
import hashlib
import json
import pathlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ranges", required=True, type=pathlib.Path)
    parser.add_argument("--extent-manifest", required=True, type=pathlib.Path)
    parser.add_argument("--out-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()

    ranges = json.loads(args.ranges.read_text())
    extent_manifest = json.loads(
        args.extent_manifest.read_text(encoding="utf-8-sig")
    )
    by_label = {}
    for row in extent_manifest:
        by_label.setdefault(row["label"], []).append(row)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    index = []
    for item in ranges["items"]:
        label = item["label"]
        rows = sorted(by_label.get(label, []), key=lambda row: row["logical"])
        data = b"".join(
            (args.extent_manifest.parent / row["file"]).read_bytes()
            for row in rows
        )
        if len(data) < item["size"]:
            raise SystemExit(f"{label}: assembled {len(data)} < {item['size']}")
        data = data[: item["size"]]
        name = label + ".bin"
        path = args.out_dir / name
        path.write_bytes(data)
        index.append(
            {
                "label": label,
                "path": item["path"],
                "inode": item["inode"],
                "size": len(data),
                "file": name,
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
        print(f"{label}: {len(data)} bytes sha256={index[-1]['sha256']}")
    (args.out_dir / "manifest.json").write_text(
        json.dumps({"schema": 1, "files": index}, indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
