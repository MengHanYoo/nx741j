#!/usr/bin/env python3
"""Build and fold the storage ABI support into build/sheng-ufs/libShengUfs.a.

The archive ends up containing:
  * IOBlockStorageDevice-reference.o  (official ABI audit reference object)
  * verified_root.o / xnu_crypto.o    (real VerifiedRoot implementation)
  * Stubs.o                           (owner-key/policy anchors + board diagnostic hook)

No kernel build-script patch is required: ShengUfs is already force-loaded by the
native-UFS branch, so appending the members to its archive makes them part of the link.
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

def compile_c(source: Path, obj: Path, extra=()):
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
                '-I', str(ROOT / '.cache/xnu-work/osfmk'),
                '-I', str(ROOT / 'kits/VerifiedRoot/include')]
    command += list(extra)
    subprocess.run(command, cwd=record['directory'], check=True, timeout=120)

compile_c(ROOT / 'kits/VerifiedRoot/src/verified_root.c', OUT / 'verified_root.o')
compile_c(ROOT / 'kits/VerifiedRoot/adapters/xnu_crypto.c', OUT / 'xnu_crypto.o')
compile_c(ROOT / 'package/storage-bringup/Stubs.c', OUT / 'Stubs.o')

archive = ROOT / 'build/sheng-ufs/libShengUfs.a'
if not archive.is_file():
    raise SystemExit('Missing build/sheng-ufs/libShengUfs.a; build the ShengUfs component first')
subprocess.run(['llvm-ar', 'r', str(archive), str(reference),
                str(OUT / 'verified_root.o'), str(OUT / 'xnu_crypto.o'), str(OUT / 'Stubs.o')],
               check=True, timeout=120)
print(f'== storage-bringup: folded into {archive}')
