#!/usr/bin/env python3
"""Assemble the local storage-bringup ABI archive.

Uses the ABI reference object produced by scripts/audit-storage-interface.py (the
official audit stops later, at its prelink-qualification evidence) plus the local
link-time stubs in package/storage-bringup/Stubs.c. Output:
build/storage-bringup/libStorageBringup.a
"""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/storage-bringup'
OUT.mkdir(parents=True, exist_ok=True)

reference = ROOT / 'build/storage-abi/IOBlockStorageDevice-reference.o'
if not reference.is_file():
    raise SystemExit('Missing build/storage-abi/IOBlockStorageDevice-reference.o; run scripts/audit-storage-interface.py')

record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
source = ROOT / 'package/storage-bringup/Stubs.c'
obj = OUT / 'Stubs.o'
command, args, i = [], record['arguments'], 0
while i < len(args):
    a = args[i]
    if a == record['file']:
        command.append(str(source))
    elif a in ('-o', '-MF', '-MT', '-MJ'):
        if a == '-o':
            command += ['-o', str(obj)]
        i += 1
    elif a == '-Xclang' and args[i + 1].startswith('-W'):
        i += 1
    elif not a.startswith(('-W', '-DXNU_BOOT_PROGRESS=')):
        command.append(a)
    i += 1
command += ['-mgeneral-regs-only', '-Wall', '-Wextra', '-Werror',
            '-I', str(ROOT / '.cache/xnu-work/osfmk')]
subprocess.run(command, cwd=record['directory'], check=True, timeout=120)

archive = OUT / 'libStorageBringup.a'
archive.unlink(missing_ok=True)
subprocess.run(['llvm-ar', 'rcs', str(archive), str(reference), str(obj)], check=True, timeout=60)
print(f'== storage-bringup: {archive}')
