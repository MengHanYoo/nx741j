#!/usr/bin/env python3
"""Create or inspect the 4 KiB tail locator for a relocated SHNGROOT image.

The locator lives in the last 4096-byte block of the userdata partition.  It
lets the kernel find a root image without a board-specific absolute LBA while
still validating that the locator belongs to the current GPT partition.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

MAGIC = b"NXROOTP1"
VERSION = 1
BLOCK_BYTES = 4096
MANIFEST_MAGIC = b"SHNGROOT"
HEADER_BYTES = 96
DIGEST_OFFSET = 96


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def sectors(image_bytes: int) -> int:
    return (image_bytes + BLOCK_BYTES - 1) // BLOCK_BYTES


def validate(anchor: bytes) -> dict[str, int | str]:
    if len(anchor) != BLOCK_BYTES:
        raise ValueError(f"anchor must be {BLOCK_BYTES} bytes, got {len(anchor)}")
    if anchor[:8] != MAGIC:
        raise ValueError("bad anchor magic")
    if u32(anchor, 8) != VERSION or u32(anchor, 12) != 0:
        raise ValueError("unsupported anchor version or flags")
    expected = hashlib.sha256(anchor[:HEADER_BYTES]).digest()
    if anchor[DIGEST_OFFSET:DIGEST_OFFSET + 32] != expected:
        raise ValueError("anchor checksum mismatch")
    partition_first = u64(anchor, 16)
    partition_last = u64(anchor, 24)
    manifest_lba = u64(anchor, 32)
    image_bytes = u64(anchor, 40)
    image_sectors = u64(anchor, 48)
    if partition_last < partition_first:
        raise ValueError("anchor partition range is reversed")
    if not image_bytes or image_bytes & (BLOCK_BYTES - 1):
        raise ValueError("anchor image size must be a nonzero multiple of 4096")
    if image_sectors != sectors(image_bytes):
        raise ValueError("anchor image sector count does not match image size")
    if manifest_lba < partition_first or manifest_lba + image_sectors - 1 > partition_last:
        raise ValueError("anchor root image lies outside its partition")
    return {
        "magic": MAGIC.decode("ascii"),
        "version": VERSION,
        "partition_first_lba": partition_first,
        "partition_last_lba": partition_last,
        "manifest_lba": manifest_lba,
        "image_bytes": image_bytes,
        "image_sectors": image_sectors,
        "manifest_sha256": anchor[56:88].hex(),
    }


def create(image: Path, manifest_lba: int, partition_first_lba: int,
           partition_last_lba: int) -> bytes:
    image_bytes = image.stat().st_size
    if not image_bytes or image_bytes & (BLOCK_BYTES - 1):
        raise ValueError("root image size must be a nonzero multiple of 4096")
    with image.open("rb") as stream:
        manifest = stream.read(BLOCK_BYTES)
    if len(manifest) != BLOCK_BYTES or manifest[:8] != MANIFEST_MAGIC:
        raise ValueError("root image does not start with a complete SHNGROOT block")
    if hashlib.sha256(manifest[:4064]).digest() != manifest[4064:4096]:
        raise ValueError("root image SHNGROOT block checksum mismatch")
    image_sectors = sectors(image_bytes)
    if manifest_lba < 0 or partition_last_lba < partition_first_lba:
        raise ValueError("invalid partition or manifest LBA")
    if manifest_lba < partition_first_lba or manifest_lba + image_sectors - 1 > partition_last_lba:
        raise ValueError("root image does not fit inside the declared partition")
    anchor = bytearray(BLOCK_BYTES)
    struct.pack_into("<8sIIQQQQQ", anchor, 0, MAGIC, VERSION, 0,
                     partition_first_lba, partition_last_lba, manifest_lba,
                     image_bytes, image_sectors)
    anchor[56:88] = hashlib.sha256(manifest).digest()
    anchor[DIGEST_OFFSET:DIGEST_OFFSET + 32] = hashlib.sha256(anchor[:HEADER_BYTES]).digest()
    return bytes(anchor)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    make = sub.add_parser("create")
    make.add_argument("--image", type=Path, required=True)
    make.add_argument("--manifest-lba", type=int, required=True)
    make.add_argument("--partition-first-lba", type=int, required=True)
    make.add_argument("--partition-last-lba", type=int, required=True)
    make.add_argument("--output", type=Path, required=True)
    show = sub.add_parser("inspect")
    show.add_argument("anchor", type=Path)
    args = parser.parse_args()

    try:
        if args.command == "create":
            data = create(args.image, args.manifest_lba,
                          args.partition_first_lba, args.partition_last_lba)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            temporary = args.output.with_name(args.output.name + ".tmp")
            temporary.write_bytes(data)
            os.replace(temporary, args.output)
            print(json.dumps(validate(data), indent=2))
        else:
            print(json.dumps(validate(args.anchor.read_bytes()), indent=2))
    except (OSError, ValueError) as error:
        print(f"userdata-anchor: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
