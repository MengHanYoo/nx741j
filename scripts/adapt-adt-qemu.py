#!/usr/bin/env python3
"""Move an already-postprocessed ADT's DRAM window to QEMU virt.

Unlike mk-qemu-adt.py, this keeps the exact payload ADT produced by
postprocess-ios-adt.py, including D22AP /product and fuxi-root-extents.
"""
import argparse
import importlib.util
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_parser():
    path = ROOT / "scripts/postprocess-ios-adt.py"
    spec = importlib.util.spec_from_file_location("ios_adt", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--ram-base", default="0x40000000")
    parser.add_argument("--ram-size", default="0x400000000")
    args = parser.parse_args()

    adt = load_parser()
    blob = Path(args.input).read_bytes()
    root, used = adt.parse(blob)
    if used != len(blob):
        raise SystemExit("input ADT has trailing bytes")
    chosen = root.child("chosen")
    if chosen is None:
        raise SystemExit("input ADT has no /chosen")
    chosen.set_prop("dram-base", struct.pack("<Q", int(args.ram_base, 0)))
    chosen.set_prop("dram-size", struct.pack("<Q", int(args.ram_size, 0)))
    adt.check_phandles(root)
    output = adt.serialize(root)
    Path(args.output).write_bytes(output)
    print(f"{args.output}: {len(output)} bytes, exact payload policy moved to "
          f"{args.ram_base}+{args.ram_size}")


if __name__ == "__main__":
    main()
