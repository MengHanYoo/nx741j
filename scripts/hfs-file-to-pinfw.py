#!/usr/bin/env python3
"""Convert hfs-inode-ranges.py output to mkadt --fw-extents JSON."""

import argparse
import json
import pathlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ranges", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    report = json.loads(args.ranges.read_text())
    if len(report["items"]) != 1:
        raise SystemExit("expected exactly one HFS item")
    item = report["items"][0]
    position = 0
    extents = []
    for row in sorted(item["ranges"], key=lambda entry: entry["logical"]):
        extents.append([position, row["physical"], row["length"], 0])
        position += row["length"]
    if position != item["size"]:
        raise SystemExit(f"mapped {position} of {item['size']} bytes")
    result = {
        "file": item["path"],
        "size": item["size"],
        "extents": extents,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{args.out}: {len(extents)} extents, {position} bytes")


if __name__ == "__main__":
    main()
