#!/usr/bin/env python3
"""Compare the nodes this repository claims against the device's live FDT.

nx741j-hw.dtsi and nx741j.dts carry a hand-copied subset of the stock device
tree.  Values drift: a register window typed one cell short, a size that was
rounded, an interrupt copied from the wrong node.  This script compiles the
device DTS, decompiles the live FDT, and compares the properties that matter
for the nodes we claim, so a wrong entry is reported instead of being found
on hardware.

    scripts/audit-live-dtb.py --live live-fdt.dtb
    scripts/audit-live-dtb.py --live live.dts --dts dts/nx741j.dts --include <soc-dts>

Exit status is 1 when a claimed node is missing or a property differs.
"""
from __future__ import annotations

import argparse
import importlib.util
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = Path(__file__).resolve().parent

spec = importlib.util.spec_from_file_location('check_bringup', HERE / 'check-bringup.py')
check_bringup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_bringup)
Tree = check_bringup.Tree

# (built path, live path) pairs the repository claims to describe.
NODES = [
    ('/ufshc@1d84000', '/soc/ufshc@1d84000'),
    ('/ufsphy_mem@1d80000', '/soc/ufsphy_mem@1d80000'),
    ('/pcie@1c00000', '/soc/pcie@1c00000'),
    ('/dwc3@a600000', '/soc/ssusb@a600000/dwc3@a600000'),
    ('/qcom,kgsl-3d0@3d00000', '/soc/qcom,kgsl-3d0@3d00000'),
    ('/spi@1a80000', '/soc/qcom,qupv3_4_geni_se@1ac0000/spi@1a80000'),
    ('/pinctrl@f000000', '/soc/pinctrl@f000000'),
    ('/sound', '/soc/spf_core_platform/sound'),
    ('/gpio_keys', '/soc/gpio_keys'),
    ('/apps-smmu@15000000', '/soc/apps-smmu@15000000'),
]

# Properties with phandle values are skipped: dtc rewrites them per build.
COMPARED = ('compatible', 'reg', 'reg-names', 'interrupts', 'interrupt-names',
            'status', '#address-cells', '#size-cells', '#interrupt-cells',
            'spi-max-frequency', 'qcom,gpu-model', 'qcom,chipid')
SKIP_VALUES = (b'disabled', b'okay')


def load(path: Path):
    if path.suffix == '.dts':
        with tempfile.NamedTemporaryFile(suffix='.dtb', delete=False) as handle:
            target = Path(handle.name)
        subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', str(target), str(path)], check=True)
        return Tree(target.read_bytes())
    if path.suffix == '.dtb':
        return Tree(path.read_bytes())
    raise SystemExit(f'{path}: expected .dts or .dtb')


def show(value):
    if value is None:
        return '-'
    if value and all(32 <= byte < 127 or byte == 0 for byte in value):
        return value.rstrip(b'\0').replace(b'\0', b',').decode()
    return '<' + ' '.join('0x%x' % word for word in struct.unpack('>%dI' % (len(value) // 4), value)) + '>' if len(value) % 4 == 0 else value.hex()


def parent_of(path):
    return path.rsplit('/', 1)[0] or '/'


def decode_reg(tree, path):
    """Decode reg into (address, size) pairs using the parent's cell widths."""
    value = tree.prop(path, 'reg')
    if not value:
        return None
    parent = parent_of(path)
    addr_cells = (tree.cells(parent, '#address-cells') or [2])[0]
    size_cells = (tree.cells(parent, '#size-cells') or [1])[0]
    words = struct.unpack('>%dI' % (len(value) // 4), value)
    step = addr_cells + size_cells
    pairs = []
    for start in range(0, len(words), step):
        chunk = words[start:start + step]
        if len(chunk) < step:
            return None
        address = 0
        for word in chunk[:addr_cells]:
            address = (address << 32) | word
        size = 0
        for word in chunk[addr_cells:]:
            size = (size << 32) | word
        pairs.append((address, size))
    return pairs


def enabled(value):
    return value is None or value.rstrip(b'\0').decode() in ('ok', 'okay')


def compare(name, live_path, built, live):
    problems = []
    if live_path not in live.nodes:
        problems.append(f'{live_path}: not in the live FDT')
        return problems
    if name not in built.nodes:
        problems.append(f'{name}: claimed node is missing from the build')
        return problems
    for prop in COMPARED:
        mine, theirs = built.prop(name, prop), live.prop(live_path, prop)
        if mine is None and theirs is None:
            continue
        if prop == 'status':
            if enabled(mine) != enabled(theirs):
                problems.append(f'{name} status: build {show(mine)} != live {show(theirs)}')
            continue
        if prop == 'reg':
            # the node is flattened to the root, so the raw cell count differs from
            # the nested stock node: compare the decoded address/size pairs instead
            mine_pairs, theirs_pairs = decode_reg(built, name), decode_reg(live, live_path)
            if mine_pairs != theirs_pairs:
                problems.append(f'{name} reg: build {mine_pairs} != live {theirs_pairs}')
            continue
        if mine != theirs:
            problems.append(f'{name} {prop}: build {show(mine)} != live {show(theirs)}')
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--live', type=Path, required=True, help='live FDT (.dtb) or its decompiled .dts')
    parser.add_argument('--dts', type=Path, default=ROOT / 'dts/nx741j.dts')
    parser.add_argument('--include', action='append', default=[], help='extra dtc include dir (SoC dts)')
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as scratch:
        built_dtb = Path(scratch) / 'hardware.dtb'
        command = ['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', str(built_dtb)]
        for directory in args.include:
            command += ['-i', directory]
        command.append(str(args.dts))
        subprocess.run(command, check=True)

        built = Tree(built_dtb.read_bytes())
        live = load(args.live)

        failures = 0
        for name, live_path in NODES:
            problems = compare(name, live_path, built, live)
            if problems:
                failures += len(problems)
                print(f'DIFF {name}')
                for problem in problems:
                    print(f'     {problem}')
            else:
                print(f'OK   {name}')

        # memory is a deliberate subset of the stock map: report, do not fail
        built_memory = decode_reg(built, '/memory')
        live_memory = decode_reg(live, '/memory')
        print(f'note memory: built {[(hex(a), hex(s)) for a, s in built_memory or []]}')
        print(f'note memory: live  {[(hex(a), hex(s)) for a, s in (live_memory or [])[:4]]}'
              f'{" ..." if live_memory and len(live_memory) > 4 else ""} ({len(live_memory or [])} ranges)')
        print()
        print(f'{len(NODES)} claimed nodes, {failures} differences')
        return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
