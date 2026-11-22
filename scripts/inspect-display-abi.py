#!/usr/bin/env python3
"""Inspect the IOMobileGraphicsFamily candidate without assuming an OS version."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--prelink-kit", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.prelink_kit))
    from native_prelink import backed_offset, exports, macho

    blob = args.kernel.read_bytes()
    plan = json.loads(args.plan.read_text())
    image = macho(blob)
    names = {name: va for va, group in exports(blob, image).items() for name in group}
    pure = names.get("___cxa_pure_virtual")
    if not pure:
        raise SystemExit("___cxa_pure_virtual is not exported")

    def read(va: int, size: int) -> bytes:
        at = backed_offset(image["segments"], va, size, len(blob))
        return blob[at:at + size]

    family = "com.apple.iokit.IOMobileGraphicsFamily"
    text = next(item for item in plan["extents"]
                if item["module"] == family and item["segment"] == "__TEXT_EXEC")
    start, end = int(text["va"]), int(text["va"]) + int(text["size"])
    found = []
    candidates = []
    for pc in range(start, end - 12, 4):
        words = struct.unpack("<3I", read(pc, 12))
        if words[0] & 0x9F00001F != 0x90000008 or words[1] & 0xFFC003FF != 0x91000108 or words[2] != 0x91004108:
            continue
        immediate = ((words[0] >> 5) & 0x7FFFF) * 4 + ((words[0] >> 29) & 3)
        if immediate & (1 << 20):
            immediate -= 1 << 21
        vptr = (pc & ~4095) + (immediate << 12) + ((words[1] >> 10) & 4095) + 16
        try:
            slots = struct.unpack("<512Q", read(vptr, 512 * 8))
        except ValueError:
            continue
        try:
            length = slots.index(0)
        except ValueError:
            continue
        if length < 160:
            continue
        active = slots[:length]
        abstract = [index for index, value in enumerate(active) if value == pure]
        candidates.append((length, abstract, pc, vptr))
        if abstract == [211, 213, 245, 246, 306, 307, 337, 338, 345]:
            found.append((pc, vptr))
    print(f"family_extent={start:#x}+{end - start:#x} pure={pure:#x}")
    for pc, vptr in found:
        print(f"legacy_constructor={pc:#x} legacy_vptr={vptr:#x}")
    for length, abstract, pc, vptr in sorted(candidates, reverse=True)[:12]:
        print(f"candidate slots={length} pure={abstract} constructor={pc:#x} vptr={vptr:#x}")
    if len(found) != 1:
        raise SystemExit(f"expected one Legacy constructor, found {len(found)}")

    constructor, vptr = found[0]
    nearby = []
    for pc in range(constructor, min(end - 12, constructor + 0x100), 4):
        words = struct.unpack("<3I", read(pc, 12))
        if words[0] & 0x9F00001F != 0x90000008 or words[1] & 0xFFC003FF != 0x91000108 or words[2] != 0x91004108:
            continue
        immediate = ((words[0] >> 5) & 0x7FFFF) * 4 + ((words[0] >> 29) & 3)
        if immediate & (1 << 20):
            immediate -= 1 << 21
        candidate = (pc & ~4095) + (immediate << 12) + ((words[1] >> 10) & 4095) + 16
        nearby.append((pc, candidate))
    print("nearby_vptr_headers=" + ",".join(f"{pc:#x}:{candidate:#x}" for pc, candidate in nearby))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
