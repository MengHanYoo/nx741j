#!/usr/bin/env python3
"""Map changed 4 KiB blocks of a pinned root image onto userdata extents.

The output describes a minimal, block-exact 9008 write. It never rewrites the
whole 16 GiB file and refuses mismatched sizes, overlapping extents, gaps, or
absolute ranges outside the declared userdata partition.
"""

import argparse
import hashlib
import json
import os
import pathlib
import struct


BLOCK = 4096
CHUNK_BLOCKS = 16384


def load_extents(path, expected_size):
    table = json.loads(path.read_text())
    if table["size"] != expected_size:
        raise SystemExit(
            f"extent table size {table['size']} != image size {expected_size}"
        )
    result = []
    logical = 0
    for raw in table["extents"]:
        if len(raw) != 4:
            raise SystemExit("extent entry does not have four fields")
        file_off, physical, length, flags = raw
        if file_off != logical:
            raise SystemExit(
                f"logical gap at {logical:#x}: next extent begins {file_off:#x}"
            )
        if physical % BLOCK or length % BLOCK or not length:
            raise SystemExit("extent is not 4 KiB aligned")
        result.append(
            {
                "file_off": file_off,
                "physical": physical,
                "length": length,
                "flags": flags,
            }
        )
        logical += length
    if logical != expected_size:
        raise SystemExit(f"extents cover {logical}, expected {expected_size}")
    physical_sorted = sorted((x["physical"], x["length"]) for x in result)
    for (start, length), (next_start, _) in zip(
        physical_sorted, physical_sorted[1:]
    ):
        if start + length > next_start:
            raise SystemExit("physical extents overlap")
    return result


def physical_for_logical(extents, logical):
    for extent in extents:
        end = extent["file_off"] + extent["length"]
        if extent["file_off"] <= logical < end:
            return extent["physical"] + logical - extent["file_off"]
    raise ValueError(f"logical offset {logical:#x} is not mapped")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, type=pathlib.Path)
    parser.add_argument("--target", required=True, type=pathlib.Path)
    parser.add_argument("--extents", required=True, type=pathlib.Path)
    parser.add_argument("--userdata-first-lba", required=True, type=int)
    parser.add_argument("--userdata-last-lba", required=True, type=int)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--extract-dir", type=pathlib.Path)
    args = parser.parse_args()

    base_size = args.base.stat().st_size
    target_size = args.target.stat().st_size
    if base_size != target_size:
        raise SystemExit(f"image size mismatch: {base_size} != {target_size}")
    extents = load_extents(args.extents, target_size)

    sha = hashlib.sha256()
    ranges = []
    current = None
    changed_blocks = 0
    changed_bytes = 0
    with args.base.open("rb", buffering=0) as base, args.target.open(
        "rb", buffering=0
    ) as target:
        for chunk_off in range(0, target_size, CHUNK_BLOCKS * BLOCK):
            target.seek(chunk_off)
            chunk = target.read(CHUNK_BLOCKS * BLOCK)
            sha.update(chunk)
            base.seek(chunk_off)
            before = base.read(len(chunk))
            if len(before) != len(chunk):
                raise SystemExit("short base read")
            for rel in range(0, len(chunk), BLOCK):
                logical = chunk_off + rel
                if before[rel : rel + BLOCK] == chunk[rel : rel + BLOCK]:
                    continue
                physical = physical_for_logical(extents, logical)
                changed_blocks += 1
                changed_bytes += BLOCK
                if (
                    current is not None
                    and current["logical_start"] + current["blocks"] * BLOCK
                    == logical
                    and current["userdata_start_lba"] + current["blocks"]
                    == args.userdata_first_lba + physical // BLOCK
                ):
                    current["blocks"] += 1
                    current["bytes"] += BLOCK
                else:
                    if current is not None:
                        ranges.append(current)
                    current = {
                        "logical_start": logical,
                        "userdata_start_lba": args.userdata_first_lba
                        + physical // BLOCK,
                        "blocks": 1,
                        "bytes": BLOCK,
                    }
        if current is not None:
            ranges.append(current)

    for index, item in enumerate(ranges):
        item["file"] = f"range-{index:04d}.bin"
        end = item["userdata_start_lba"] + item["blocks"] - 1
        if (
            item["userdata_start_lba"] < args.userdata_first_lba
            or end > args.userdata_last_lba
        ):
            raise SystemExit(
                f"{item['file']} is outside userdata: {item['userdata_start_lba']}..{end}"
            )

    if args.extract_dir:
        args.extract_dir.mkdir(parents=True, exist_ok=True)
        with args.target.open("rb", buffering=0) as target:
            for item in ranges:
                target.seek(item["logical_start"])
                data = target.read(item["bytes"])
                if len(data) != item["bytes"]:
                    raise SystemExit(f"short target read for {item['file']}")
                (args.extract_dir / item["file"]).write_bytes(data)

    report = {
        "schema": 1,
        "base": str(args.base),
        "target": str(args.target),
        "extents": str(args.extents),
        "target_size": target_size,
        "target_sha256": sha.hexdigest(),
        "changed_blocks": changed_blocks,
        "changed_bytes": changed_bytes,
        "range_count": len(ranges),
        "userdata_first_lba": args.userdata_first_lba,
        "userdata_last_lba": args.userdata_last_lba,
        "ranges": ranges,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"changed_blocks={changed_blocks} changed_bytes={changed_bytes} "
        f"ranges={len(ranges)} sha256={report['target_sha256']}"
    )


if __name__ == "__main__":
    main()
