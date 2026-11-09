#!/usr/bin/env python3
"""Convert an IPSW trust-cache IM4P into the flat bundle DarwinBoot validates.

The firmware (ValidTrustCaches) expects: u32 count, count * u32 absolute
offsets, then the blobs; every blob starts with u32 magic 2.  The IPSW ships
a DER-wrapped IM4P instead, so the payload needs this conversion once.
"""
import struct
import sys
from pathlib import Path


def der_item(data, at):
    if at + 2 > len(data):
        raise ValueError('truncated DER')
    tag, size = data[at], data[at + 1]
    at += 2
    if size & 0x80:
        count = size & 0x7F
        if not 0 < count <= 4 or at + count > len(data):
            raise ValueError('bad DER length')
        size = int.from_bytes(data[at:at + count], 'big')
        at += count
    if at + size > len(data):
        raise ValueError('DER value exceeds input')
    return tag, data[at:at + size], at + size


def main(argv):
    if len(argv) != 3:
        raise SystemExit('usage: mk-trustcache-bundle.py IM4P OUT')
    src, dst = Path(argv[1]), Path(argv[2])
    data = src.read_bytes()
    tag, sequence, end = der_item(data, 0)
    if tag != 0x30 or end != len(data):
        raise SystemExit(f'{src}: not a single DER SEQUENCE')
    fields, cursor = [], 0
    while cursor < len(sequence):
        t, value, cursor = der_item(sequence, cursor)
        fields.append((t, value))
    kinds = [t for t, _ in fields]
    if kinds != [0x16, 0x16, 0x16, 0x04] or fields[0][1] != b'IM4P':
        raise SystemExit(f'{src}: unexpected IM4P layout {kinds}')
    blob = fields[3][1]
    if len(blob) < 24 or struct.unpack_from('<I', blob, 0)[0] != 2:
        raise SystemExit(f'{src}: payload does not start with the trust-cache magic')
    bundle = struct.pack('<2I', 1, 8) + blob
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bundle)
    print(f'{src.name}: IM4P payload {len(blob)} B -> bundle {len(bundle)} B ({dst})')


if __name__ == '__main__':
    main(sys.argv)
