#!/usr/bin/env python3
"""Map HFS+ metadata fork extents through a pinfile table to userdata LBAs."""

import argparse
import json
import pathlib
import struct


BLOCK = 4096
FORK_OFFSETS = {
    "allocation": 112,
    "extents": 192,
    "catalog": 272,
    "attributes": 352,
}


def load_extents(path):
    table = json.loads(path.read_text())
    ranges = []
    for logical, physical, length, _flags in table["extents"]:
        ranges.append(
            {
                "logical": logical,
                "physical": physical,
                "length": length,
            }
        )
    return table["size"], ranges


def map_range(root_size, root_ranges, logical, length):
    result = []
    end = logical + length
    position = logical
    for extent in root_ranges:
        extent_end = extent["logical"] + extent["length"]
        if position >= extent_end or end <= extent["logical"]:
            continue
        take_start = max(position, extent["logical"])
        take_end = min(end, extent_end)
        take = take_end - take_start
        physical = extent["physical"] + take_start - extent["logical"]
        result.append(
            {
                "logical": take_start,
                "physical": physical,
                "length": take,
            }
        )
        position = take_end
    if position != end:
        raise SystemExit(
            f"HFS range {logical:#x}+{length:#x} is not fully covered by root extents"
        )
    if end > root_size:
        raise SystemExit("HFS range exceeds pinned root size")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", required=True, type=pathlib.Path)
    parser.add_argument("--extents", required=True, type=pathlib.Path)
    parser.add_argument("--userdata-first-lba", required=True, type=int)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    header_block = args.header.read_bytes()
    if len(header_block) != BLOCK:
        raise SystemExit("header input must be one 4 KiB allocation block")
    if header_block[1024:1026] != b"HX":
        raise SystemExit("not an HFSX volume header")
    header = header_block[1024:1536]
    root_size, root_ranges = load_extents(args.extents)

    items = {}
    root0 = {
        "name": "volume_header",
        "ranges": map_range(
            root_size,
            root_ranges,
            0,
            BLOCK,
        ),
    }
    for inner in root0["ranges"]:
        inner["userdata_lba"] = args.userdata_first_lba + inner["physical"] // BLOCK
    items["volume_header"] = root0

    for name, offset in FORK_OFFSETS.items():
        logical_size, clump_size, total_blocks = struct.unpack_from(
            ">QII", header, offset
        )
        extents = []
        for index in range(8):
            start_block, block_count = struct.unpack_from(
                ">II", header, offset + 16 + index * 8
            )
            if block_count:
                extents.append((start_block, block_count))
        mapped = []
        for start_block, block_count in extents:
            for inner in map_range(
                root_size,
                root_ranges,
                start_block * BLOCK,
                block_count * BLOCK,
            ):
                inner["hfs_start_block"] = start_block
                inner["userdata_lba"] = (
                    args.userdata_first_lba + inner["physical"] // BLOCK
                )
                mapped.append(inner)
        items[name] = {
            "name": name,
            "logical_size": logical_size,
            "clump_size": clump_size,
            "total_blocks": total_blocks,
            "extents": extents,
            "ranges": mapped,
        }

    report = {
        "schema": 1,
        "header": str(args.header),
        "extents": str(args.extents),
        "userdata_first_lba": args.userdata_first_lba,
        "items": items,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    for name, item in items.items():
        total = sum(row["length"] for row in item["ranges"])
        range_count = len(item["ranges"])
        print(f"{name}: {total} bytes in {range_count} mapped ranges")
    print(args.out)


if __name__ == "__main__":
    main()
