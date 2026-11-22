#!/usr/bin/env python3
"""Create the sparse 4K-GPT rawdump disk used by the P7 loader test."""

import argparse
import binascii
import os
import struct
import uuid
from pathlib import Path


SECTOR = 4096
ENTRY_COUNT = 128
ENTRY_SIZE = 128
ENTRY_SECTORS = (ENTRY_COUNT * ENTRY_SIZE) // SECTOR


def guid_le(text):
    return uuid.UUID(text).bytes_le


def make_entry(name, first_lba, last_lba):
    entry = bytearray(ENTRY_SIZE)
    entry[0:16] = guid_le("0fc63daf-8483-4772-8e79-3d69d8477de4")
    entry[16:32] = guid_le("4e584a37-4a31-5037-0001-000000000001")
    struct.pack_into("<QQQ", entry, 32, first_lba, last_lba, 0)
    encoded = name.encode("utf-16le")
    entry[56 : 56 + len(encoded)] = encoded
    return bytes(entry)


def make_header(current_lba, backup_lba, entries_lba, entries_crc, last_usable):
    header = bytearray(SECTOR)
    header[0:8] = b"EFI PART"
    struct.pack_into("<IIII", header, 8, 0x00010000, 92, 0, 0)
    struct.pack_into("<QQQQ", header, 24, current_lba, backup_lba, 34, last_usable)
    header[56:72] = guid_le("6e584a37-4a31-5037-0001-000000000000")
    struct.pack_into("<QIII", header, 72, entries_lba, ENTRY_COUNT, ENTRY_SIZE, entries_crc)
    crc = binascii.crc32(header[:92]) & 0xFFFFFFFF
    struct.pack_into("<I", header, 16, crc)
    return header


def write_at(fs, offset, data):
    fs.seek(offset)
    fs.write(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--disk", required=True)
    parser.add_argument("--payload", required=True)
    parser.add_argument("--fd", required=True)
    parser.add_argument("--rawdump-lba", type=int, default=4743976)
    parser.add_argument("--rawdump-sectors", type=int, default=131072)
    parser.add_argument("--size-gib", type=int, default=24)
    args = parser.parse_args()

    total_lbas = args.size_gib * (1024**3) // SECTOR
    last_lba = total_lbas - 1
    backup_entries_lba = last_lba - ENTRY_SECTORS
    payload = Path(args.payload).read_bytes()
    fd = Path(args.fd).read_bytes()
    if len(payload) != 64 * 1024 * 1024:
        raise SystemExit("payload must be exactly 64 MiB")
    if len(fd) != 4 * 1024 * 1024:
        raise SystemExit("FD must be exactly 4 MiB")

    first_lba = args.rawdump_lba
    part_last_lba = first_lba + args.rawdump_sectors - 1
    if part_last_lba >= last_lba - ENTRY_SECTORS - 1:
        raise SystemExit("rawdump partition does not fit")

    entries = bytearray(ENTRY_COUNT * ENTRY_SIZE)
    entries[0:ENTRY_SIZE] = make_entry("rawdump", first_lba, part_last_lba)
    entries_crc = binascii.crc32(entries) & 0xFFFFFFFF

    disk = Path(args.disk)
    disk.parent.mkdir(parents=True, exist_ok=True)
    with disk.open("w+b") as fs:
        fs.truncate(args.size_gib * (1024**3))
        write_at(
            fs,
            SECTOR,
            make_header(1, last_lba, 2, entries_crc, last_lba - 34),
        )
        write_at(fs, 2 * SECTOR, entries)
        write_at(fs, first_lba * SECTOR, payload)
        write_at(fs, first_lba * SECTOR + len(payload), fd)
        write_at(fs, backup_entries_lba * SECTOR, entries)
        write_at(
            fs,
            last_lba * SECTOR,
            make_header(
                last_lba,
                1,
                backup_entries_lba,
                entries_crc,
                last_lba - 34,
            ),
        )

    print(f"disk={disk} size={os.path.getsize(disk)} rawdump={first_lba}")


if __name__ == "__main__":
    main()
