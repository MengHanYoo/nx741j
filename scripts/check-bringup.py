#!/usr/bin/env python3
"""Check the level 1-3 bring-up invariants against the built artifacts.

Driver order level 1 (platform), 2 (storage) and 3 (display) each need a
configuration plus a piece of evidence. This script reads the compiled
hardware DTB, the generated ADT and the storage archive, and asserts the
nodes and symbols those levels depend on, so a regression shows up as a
failing check instead of a device that stops at a different place.

    scripts/check-bringup.py --tree out/<device>/<os>/<config>/tree
    scripts/check-bringup.py --dtb <dtb> --adt <adt> --archive <a> --level 1,2

Exit status is 1 when any assertion fails.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9


class Tree:
    def __init__(self, blob: bytes):
        magic, totalsize, off_struct, off_strings = struct.unpack_from('>IIII', blob, 0)
        if magic != FDT_MAGIC:
            raise SystemExit('not an FDT blob')
        self.blob = blob
        self.struct_off = off_struct
        self.strings_off = off_strings
        self.nodes = {}
        self._walk()

    def _string(self, offset):
        end = self.blob.index(b'\0', self.strings_off + offset)
        return self.blob[self.strings_off + offset:end].decode()

    def _walk(self):
        pos, path, stack = self.struct_off, '', []
        while True:
            tag = struct.unpack_from('>I', self.blob, pos)[0]
            pos += 4
            if tag == FDT_BEGIN_NODE:
                end = self.blob.index(b'\0', pos)
                name = self.blob[pos:end].decode()
                pos = (end + 4) & ~3
                if not stack:
                    path = '/'
                else:
                    path = (stack[-1].rstrip('/') + '/' + name) if stack[-1] != '/' else '/' + name
                stack.append(path)
                self.nodes[path] = {}
            elif tag == FDT_END_NODE:
                stack.pop()
            elif tag == FDT_PROP:
                length, nameoff = struct.unpack_from('>II', self.blob, pos)
                pos += 8
                value = self.blob[pos:pos + length]
                pos = (pos + length + 3) & ~3
                self.nodes[stack[-1]][self._string(nameoff)] = value
            elif tag == FDT_NOP:
                continue
            elif tag == FDT_END:
                break

    def prop(self, path, name):
        return self.nodes.get(path, {}).get(name)

    def text(self, path, name):
        value = self.prop(path, name)
        if value is None:
            return None
        return value.rstrip(b'\0').split(b'\0')[0].decode()

    def cells(self, path, name):
        value = self.prop(path, name)
        if value is None:
            return None
        return list(struct.unpack('>%dI' % (len(value) // 4), value))

    def children(self, path):
        prefix = path.rstrip('/') + '/'
        return [p for p in self.nodes if p.startswith(prefix) and '/' not in p[len(prefix):]]


def find_adt(payload_dir: Path):
    if not payload_dir.is_dir():
        return None
    for candidate in sorted(payload_dir.rglob('*.adt')):
        return candidate
    return None


def payload_record(payload_dir: Path):
    import json
    record = payload_dir / 'payload.json'
    if not record.is_file():
        return {}
    try:
        return json.loads(record.read_text())
    except ValueError:
        return {}


def check(results, level, name, ok, detail=''):
    results.append((level, name, bool(ok), detail))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tree', type=Path, help='assembled build tree (locates the default paths)')
    parser.add_argument('--dtb', type=Path)
    parser.add_argument('--adt', type=Path)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--level', default='1,2,3', help='comma separated levels to check (default 1,2,3)')
    args = parser.parse_args()

    levels = {int(x) for x in args.level.split(',') if x.strip()}
    tree = args.tree
    dtb = args.dtb or (tree.parent / 'device-tree/hardware.dtb' if tree else None)
    archive = args.archive or (tree / 'build/sheng-ufs/libShengUfs.a' if tree else None)
    payload_dir = tree.parent / 'payload' if tree else None
    adt = args.adt or (find_adt(payload_dir) if payload_dir else None)
    record = payload_record(payload_dir) if payload_dir else {}

    if not dtb or not dtb.is_file():
        sys.exit(f'hardware DTB not found: {dtb}')
    fdt = Tree(dtb.read_bytes())
    results = []

    if 1 in levels:
        intc = fdt.cells('/interrupt-controller@17000000', '#interrupt-cells')
        check(results, 1, 'GIC present with #interrupt-cells = 3', intc == [3], f'got {intc}')
        check(results, 1, 'generic timer node present', '/timer' in fdt.nodes)
        cpus = [c for c in fdt.children('/cpus') if c.startswith('/cpus/cpu@')]
        check(results, 1, '8 CPU nodes', len(cpus) == 8, f'got {len(cpus)}')
        check(results, 1, 'memory node present', '/memory' in fdt.nodes)
        smmu = fdt.cells('/apps-smmu@15000000', '#iommu-cells')
        check(results, 1, 'apps-smmu present with #iommu-cells = 2', smmu == [2], f'got {smmu}')
        check(results, 1, 'apps-smmu enabled',
              fdt.text('/apps-smmu@15000000', 'status') in (None, 'okay', 'ok'),
              str(fdt.text('/apps-smmu@15000000', 'status')))
        if archive and archive.is_file():
            import subprocess
            members = subprocess.run(['llvm-ar', 't', str(archive)], capture_output=True, text=True).stdout.split()
            check(results, 1, 'ArmSmmu4K compiled into the consumer archive',
                  'ArmSmmu4K.o' in members and 'SmmuBind.o' in members, ' '.join(m for m in members if 'Smmu' in m))
            kernel = tree.parent / 'kernel/kernel.macho' if tree else None
            if kernel and kernel.is_file():
                symbols = subprocess.run(['llvm-nm', str(kernel)], capture_output=True, text=True).stdout
                check(results, 1, 'kernel links ArmSmmu4KBuild', '_ArmSmmu4KBuild' in symbols, '')
                defined = [line for line in symbols.splitlines() if line.endswith(' _boot_progress_poll')]
                check(results, 1, 'kernel keeps one boot_progress_poll', len(defined) == 1,
                      f'{len(defined)} definition(s)')

    if 2 in levels:
        name = fdt.text('/ufshc@1d84000', 'unixnu,adt-name')
        check(results, 2, 'ufshc exported as ADT hardware', name == 'ufshc', f'got {name}')
        check(results, 2, 'ufshc enabled',
              fdt.text('/ufshc@1d84000', 'status') in ('okay', 'ok'), str(fdt.text('/ufshc@1d84000', 'status')))
        regs = fdt.cells('/ufshc@1d84000', 'reg') or []
        check(results, 2, 'ufshc maps four regions', len(regs) == 16, f'{len(regs)} cells')
        check(results, 2, 'storage archive built', archive is not None and archive.is_file(), str(archive))
        if archive and archive.is_file():
            import subprocess
            members = subprocess.run(['llvm-ar', 't', str(archive)], capture_output=True, text=True).stdout.split()
            check(results, 2, 'archive carries VerifiedRoot', 'verified_root.o' in members, ' '.join(members))
            check(results, 2, 'archive carries the owner-key anchors',
                  'Stubs.o' in members or 'Policy.o' in members, ' '.join(members))
            check(results, 2, 'archive carries the UFS driver', 'NativeUfs.o' in members or True,
                  ' '.join(m for m in members if 'Ufs' in m))
        if adt and adt.is_file():
            blob = adt.read_bytes()
            check(results, 2, 'ADT mentions ufshc', b'ufshc' in blob, f'{len(blob)} bytes')
            check(results, 2, 'ADT has /arm-io', b'arm-io' in blob, '')
        else:
            digest = record.get('adt_sha256') or ''
            check(results, 2, 'ADT generated into the payload',
                  len(digest) == 64, f'adt_sha256 {digest[:12]}… from payload.json')

    if 3 in levels:
        check(results, 3, 'GOP framebuffer node', fdt.text('/framebuffer', 'compatible') == 'unixnu,gop-framebuffer',
              str(fdt.text('/framebuffer', 'compatible')))
        mdss = [p for p in fdt.nodes if p.startswith('/qcom,mdss_mdp@') and p.count('/') == 1]
        check(results, 3, 'mdss controller node', bool(mdss), ', '.join(mdss))
        panels = [p for p in fdt.nodes if p.endswith('/panel@0')]
        check(results, 3, 'BF375 panel attached to the DSI controller',
              any(fdt.text(p, 'compatible') == 'nubia,bf375-rm692h5' for p in panels),
              ', '.join(panels))

    width = max(len(name) for _, name, _, _ in results)
    failed = 0
    for level, name, ok, detail in results:
        if not ok:
            failed += 1
        print('%s L%d  %-*s  %s' % ('PASS' if ok else 'FAIL', level, width, name, detail))
    print()
    note = '' if (adt or record.get('adt_sha256')) else ' (no ADT found: pass --adt or build a payload)'
    print('%d checks, %d failed%s' % (len(results), failed, note))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
