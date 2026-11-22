#!/usr/bin/env python3
"""Resolve HFS+ catalog inode data extents to userdata LBAs."""

import argparse
import json
import pathlib
import re
import subprocess


BLOCK = 4096
ADDRESS = re.compile(r"Starting address:\s*(\d+),\s*length:\s*(\d+)")
SIZE = re.compile(r"^Size:\s*(\d+)$", re.MULTILINE)
PATH = re.compile(r"^File Path:\s*(.+)$", re.MULTILINE)


def root_ranges(path):
    table = json.loads(path.read_text())
    return table["size"], [
        {
            "logical": logical,
            "physical": physical,
            "length": length,
        }
        for logical, physical, length, _flags in table["extents"]
    ]


def map_range(size, ranges, logical, length):
    output = []
    position = logical
    end = logical + length
    for extent in ranges:
        extent_end = extent["logical"] + extent["length"]
        if position >= extent_end or end <= extent["logical"]:
            continue
        start = max(position, extent["logical"])
        stop = min(end, extent_end)
        physical = extent["physical"] + start - extent["logical"]
        output.append(
            {
                "logical": start,
                "physical": physical,
                "length": stop - start,
            }
        )
        position = stop
    if position != end or end > size:
        raise SystemExit("inode range is not covered by pinned root extents")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    parser.add_argument("--extents", required=True, type=pathlib.Path)
    parser.add_argument("--userdata-first-lba", required=True, type=int)
    parser.add_argument(
        "--inode",
        action="append",
        required=True,
        help="LABEL:INODE",
    )
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    size, ranges = root_ranges(args.extents)
    items = []
    for spec in args.inode:
        label, inode_text = spec.split(":", 1)
        inode = int(inode_text, 0)
        text = subprocess.check_output(
            ["istat", "-f", "hfs", "-r", str(args.snapshot), str(inode)],
            text=True,
        )
        path_match = PATH.search(text)
        size_match = SIZE.search(text)
        if not path_match or not size_match:
            raise SystemExit(f"cannot parse inode {inode}")
        extents = [(int(a), int(b)) for a, b in ADDRESS.findall(text)]
        if not extents and int(size_match.group(1)) != 0:
            raise SystemExit(f"inode {inode} has no data extents")
        mapped = []
        for address, count in extents:
            for inner in map_range(size, ranges, address * BLOCK, count * BLOCK):
                inner["userdata_lba"] = (
                    args.userdata_first_lba + inner["physical"] // BLOCK
                )
                mapped.append(inner)
        items.append(
            {
                "label": label,
                "inode": inode,
                "path": path_match.group(1),
                "size": int(size_match.group(1)),
                "hfs_extents": extents,
                "ranges": mapped,
            }
        )
        print(f"{label}: inode {inode} size {size_match.group(1)} ranges {len(mapped)}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps(
            {
                "schema": 1,
                "snapshot": str(args.snapshot),
                "userdata_first_lba": args.userdata_first_lba,
                "items": items,
            },
            indent=2,
        )
        + "\n"
    )
    print(args.out)


if __name__ == "__main__":
    main()
