#!/usr/bin/env python3
"""Build the QEMU ADT from the device's own device tree.

The payload ADT describes the phone: DRAM at 0x8C0000000, the Qualcomm UFS
controller, the apps SMMU and so on.  QEMU virt has none of that, so the ADT
cannot be used as-is: the kernel's pmap would map memory that does not exist.

This script keeps every device node (cpus, gic, ufshc, smmu, clocks, /chosen
identity) and rewrites only the DRAM window to QEMU's RAM (0x40000000, 16 GiB),
which is the one parameter a machine model cannot relocate.  What the kernel
then does with the rest is the interesting part: it maps the real addresses and
stops where the model has no device.

    scripts/mk-qemu-adt.py --device <repo> --out <file.adt> [--ram-base 0x40000000] [--ram-size 0x400000000]
"""
import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile
import tomllib

FRAGMENT = """
/* QEMU model: the only parameter a machine model cannot move is DRAM, so the
   /memory node is rewritten to QEMU virt's RAM while every device node keeps
   the phone's real addresses. */
/ {
    memory {
        reg = <%s %s %s %s>;
    };
};
"""


def cells(address, size):
    return (f"0x{address >> 32:x}", f"0x{address & 0xffffffff:x}",
            f"0x{size >> 32:x}", f"0x{size & 0xffffffff:x}")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--ram-base', default='0x40000000')
    parser.add_argument('--ram-size', default='0x400000000')
    parser.add_argument('--seed', default='0x5eed5eed5eed5eed')
    args = parser.parse_args(argv[1:])

    device = pathlib.Path(args.device)
    source = device / 'dts/nx741j-adt.dts'
    if not source.is_file():
        raise SystemExit(f'{source}: run scripts/mk-adt-dts.py first')
    base = int(args.ram_base, 0)
    size = int(args.ram_size, 0)
    if base % 0x10000000 or size % 0x10000000:
        raise SystemExit('the ADT generator aligns DRAM to 256 MiB')

    tree = pathlib.Path('/home/loongko/nx741j-build/UniXNU')
    sys.path.insert(0, str(tree / 'scripts'))
    from unixnu import devicetree  # noqa: E402

    config = tomllib.load(open(device / 'board.toml', 'rb'))
    config.setdefault('payload', {})['dram_size'] = hex(size)

    with tempfile.TemporaryDirectory() as work:
        dts = pathlib.Path(work) / 'qemu.dts'
        dtb = pathlib.Path(work) / 'qemu.dtb'
        dts.write_text(source.read_text() + FRAGMENT % cells(base, size))
        subprocess.run(['dtc', '-I', 'dts', '-O', 'dtb', '-o', str(dtb), str(dts)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        adt = devicetree.generate(dtb, config, unique_chip_id=args.seed, data=False, trust=True, addon=False)

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(adt)
    print(f'{out}: {len(adt)} B, DRAM {hex(base)}+{hex(size)}, device nodes kept from {source.name}')


if __name__ == '__main__':
    main(sys.argv)
