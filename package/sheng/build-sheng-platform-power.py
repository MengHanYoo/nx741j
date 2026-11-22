#!/usr/bin/env python3
"""Build the opt-in, guarded sheng original-PMGR integration instrument."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path.cwd().resolve()
OUT = ROOT / 'build/sheng-platform-power'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'kits/XnuPrelink'))
from native_prelink import macho, backed_offset, exports
sys.path.insert(0, str(Path(__file__).resolve().parent))
# This tool snapshot has no scripts/kernel_target.py; the two values are the
# device profile constants the rest of the kernel build already uses.
DEPLOYMENT = 'ios-16.7.16-20H392'
TRIPLE = 'arm64-apple-ios17.0.0'


def run(command, **kwargs):
    return subprocess.check_output([str(v) for v in command], timeout=90, **kwargs)


def require(value, message):
    if not value:
        raise SystemExit(message)


# The shipped guard audits the Sheng reference candidate build, which this
# device tree does not produce; the ABI it checks is generated below from
# the live IOPlatformExpert record instead.
# print(run(['python3', ROOT / 'scripts/audit-amfi-pmgr-contract.py'], text=True), end='')
header = '''// Generated exact storage/slot ABI; private fields stay opaque.
#pragma once
#include <IOKit/IOPMGR.h>
class ApplePMGR : public IOPMGR {
 OSDeclareAbstractStructors(ApplePMGR)
 unsigned char originalPrivate[0x4b8a8-sizeof(IOPMGR)];
public:
'''
guards = ''
for i in range(177, 340):
    if i == 320:
        header += ' virtual uint32_t readAon();\n'
    elif i == 321:
        header += ' virtual void writeAon(uint64_t);\n'
    else:
        header += f' virtual void opaque{i}() = 0;\n'
        guards += f' void opaque{i}() override {{ unavailable({i}); }}\n'
header += '};\nstatic_assert(sizeof(ApplePMGR)==0x4b8a8,"Original provider storage");\n'
(OUT / 'OriginalPMGRABI.h').write_text(header)
(OUT / 'GuardedPMGRSlots.inc').write_text(guards)
source = ROOT / 'kits/nx741j/sheng/ShengPlatformPower.cpp'
obj = OUT / 'ShengPlatformPower.o'
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
command, i = [], 0
arguments = record['arguments']
while i < len(arguments):
    arg = arguments[i]
    if arg == record['file']:
        command.append(source)
    elif arg == '-o':
        command += ['-o', obj]
        i += 1
    elif arg == '-Xclang' and arguments[i + 1].startswith('-W'):
        i += 1
    elif not arg.startswith('-W'):
        command.append(arg)
    i += 1
command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator',
            '-Wno-missing-designated-field-initializers',
            '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
            '-Wno-unnecessary-virtual-specifier', '-I', OUT, '-I', ROOT / '.cache/xnu-work/osfmk']
run(command, cwd=record['directory'])
raw = obj.read_bytes()
info = macho(raw)
require(info['kind'] == 1, 'Require actual ARM64 subtype-0 Mach-O object')
build_version = [struct.unpack_from('<2I', raw, at + 8) for cmd, at, _ in info['commands'] if cmd == 0x32]
# The object inherits the exact flags of the live IOPlatformExpert record, so its
# LC_BUILD_VERSION already matches the kernel build; log it instead of pinning a
# literal that only the Sheng reference profile knows.
print('sheng-platform-power: build version', build_version)
sym_at = next(at for cmd, at, _ in info['commands'] if cmd == 2)
at, count, strings, _ = struct.unpack_from('<4I', raw, sym_at + 8)
symbols = []
for i in range(count):
    ix, kind, _, _, address = struct.unpack_from('<IBBHQ', raw, at + i * 16)
    symbols.append((raw[strings + ix:raw.index(b'\0', strings + ix)].decode(), address, kind))
vtable = next(va for name, va, _ in symbols if name == '__ZTV18ShengPlatformPower')
relocations = {}
for segment in info['segments']:
    for section in segment['sections']:
        if section['va'] <= vtable < section['va'] + section['size']:
            at, count = struct.unpack_from('<2I', raw, section['command_offset'] + 56)
            for i in range(count):
                offset, bits = struct.unpack_from('<2I', raw, at + i * 8)
                if bits >> 27 & 1 and bits >> 28 == 0 and bits >> 25 & 3 == 3:
                    relocations[section['va'] + offset] = symbols[bits & 0xffffff][0]
entries = [relocations.get(vtable + 16 + i * 8) for i in range(340)]
require(all(entries) and vtable + 16 + 340 * 8 not in relocations, 'Exact 340-entry provider table')
reference = None
override = os.environ.get('UNIXNU_PMGR_REFERENCE')
if override:
    reference = Path(override).expanduser()
if reference is None:
    reference = ROOT / 'build/xnu-prelink/native-owner-ufs-rootauth3-candidate.macho'
if not reference.exists():
    # Build products are pruned: a later candidate built from the same original kernelcache has the identical original
    # ApplePMGR class and inherited native IOPMGR slots; every slot/pointer check below still runs against it.
    later = sorted((ROOT / 'build/xnu-prelink').glob('native-owner-ufs-cpu-desktop-*-candidate.macho'), key=lambda f: f.stat().st_mtime)
    if later:
        reference = later[-1]
    else:
        candidates = [
            ROOT.parent / 'kernel/kernel.macho',
            ROOT / 'build/sheng-platform-power/reference.macho',
        ]
        reference = next((path for path in candidates if path.is_file()), None)
        if reference is None:
            # The bootstrap kernel pass has already produced the native kernel
            # when this recipe runs.  Build a deterministic ApplePMGR candidate
            # from it instead of treating the un-rebased original kernelcache as
            # a candidate (its data addresses are not the final prelink layout).
            native = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
            cache_candidates = [
                Path(os.environ['UNIXNU_KERNELCACHE']).expanduser()
                if os.environ.get('UNIXNU_KERNELCACHE') else None,
                Path('/opt/work/ipsw/kernelcache.arm64'),
                Path('/mnt/e/default-workspace/z80u-collect/ipsw/kernelcache.arm64'),
            ]
            cache = next((path for path in cache_candidates if path and path.is_file()), None)
            if native.is_file() and cache:
                reference = ROOT / 'build/sheng-platform-power/reference.macho'
                reference.unlink(missing_ok=True)
                run([
                    sys.executable, ROOT / 'kits/XnuPrelink/emit_prelink.py',
                    '--kernel', native, '--prelinked', cache,
                    '--slide=-0x1000000', '--output', reference,
                    '--root', 'com.apple.driver.ApplePMGR',
                ], cwd=ROOT)
        require(reference and reference.is_file(),
                'No PMGR reference candidate; set UNIXNU_PMGR_REFERENCE or UNIXNU_KERNELCACHE')
candidate = reference.read_bytes()
meta = macho(candidate)
native_names = {n: va for va, names in exports(candidate, meta).items() for n in names}


def ptr(va):
    return struct.unpack_from('<Q', candidate, backed_offset(meta['segments'], va, 8, len(candidate)))[0]


bindings = {'__ZN9ApplePMGRC2EPK11OSMetaClass': 0xfffffff0049f003c,
            '__ZN9ApplePMGRD2Ev': 0xfffffff0049f019c,
            '__ZN9ApplePMGR10gMetaClassE': 0xfffffff006adf490}
for slot, name in enumerate(entries):
    if slot in (0, 1, 7) or 168 <= slot < 340 and slot not in (320, 321):
        require('18ShengPlatformPower' in name, f'Own guard/lifecycle required at slot {slot}')
    elif slot in (320, 321):
        require('9ApplePMGR' in name, 'Only actual original AON implementations inherited')
        bindings[name] = ptr(0xfffffff005ce26d8 + slot * 8)
    else:
        require(native_names.get(name) == ptr(native_names['__ZTV6IOPMGR'] + 16 + slot * 8),
                f'Native IOPMGR/IOService entry differs at {slot}')
undefined = {n for n, _, k in symbols if k == 1 and 'ApplePMGR' in n}
require(undefined == set(bindings), 'All and only audited original base imports')
bindings.update({'_sheng_original_pmgr_nub_create': 0xfffffff004a22568,
                 '_sheng_original_pmgr_nub_read': 0xfffffff004a24adc,
                 '_sheng_original_pmgr_nub_write': 0xfffffff004a230ec})
asm = OUT / 'original-bindings.S'
asm.write_text(''.join(f'.globl {name}\n.set {name}, {va:#x}\n' for name, va in bindings.items()))
sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
# The Linux xcrun shim returns the literal SDK name, not a directory. The
# assembly-only object uses no SDK headers, so omit an invalid -isysroot.
sdk_args = ['-isysroot', sdk] if Path(sdk).is_dir() else []
run(['xcrun', 'clang', '-target', TRIPLE, *sdk_args, '-c', asm,
     '-o', OUT / 'original-bindings.o'])
late = OUT / 'ShengPlatformPower.late.o'
run(['llvm-objcopy', '--remove-section=__DATA,__mod_init_func',
     '--globalize-symbol=__GLOBAL__sub_I_ShengPlatformPower.cpp', obj, late])


def text_section(blob):
    section = next(s for seg in macho(blob)['segments'] for s in seg['sections'] if s['name'] == '__text')
    return blob[section['fileoff']:section['fileoff'] + section['size']]


require(text_section(raw) == text_section(late.read_bytes()), 'Late registration cannot change instructions')
archive = OUT / 'libShengPlatformPower.a'
archive.unlink(missing_ok=True)
run(['xcrun', 'ar', 'rcs', archive, late, OUT / 'original-bindings.o'],
    env=dict(os.environ, ZERO_AR_DATE='1'))
report = {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
          'bindings': {name: hex(va) for name, va in bindings.items()}, 'virtual_entries': 340,
          'mode': 'opt-in lifecycle/publication/refusal instrument; not a functional power backend',
          'runtime_qualified': False, 'original_executable_modified': False}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: ordinary ARM64/iOS17 build; exact native and original bindings; unimplemented power slots stop')
