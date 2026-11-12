#!/usr/bin/env python3
"""Fix the adopt path cache clean beyond the mapping in ShengUfs.

NativeUfs::adoptAndRead maps the firmware request list with a 16 KiB window but
cleans 32 KiB.  The clean therefore walks past the mapping and takes a level-3
translation fault (pc adoptAndRead+0x844, far = map base + 0x4000, esr 0x96000047).
Only the first 16 KiB holds the UTRD/UCD/data this driver uses, so clean exactly
that window.  The anchor is the zero-then-clean-then-bypass sequence, which is
unique to the adopt path (two other sites clean 32 KiB legitimately).

The driver is an upstream component, so the fix is applied after the fetch and
before component:ShengUfs compiles.  Re-running the step is harmless.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/ShengUfs/NativeUfs.cpp'
WRONG = '  cacheClean (virt, 16384);   /* only the mapped window; 32768 faults (2026-11-12) */'
BROKEN = ('  zero32 (virt, 4096);\n'
          '  cacheClean (virt, 32768);\n'
          '  smmuBypassStream (0x60, smmuMap, smmuReport, phys, 16384);')
FIXED = ('  zero32 (virt, 4096);\n'
         '  cacheClean (virt, 16384);   /* mapped window only; 32768 faults (2026-11-12) */\n'
         '  smmuBypassStream (0x60, smmuMap, smmuReport, phys, 16384);')

if not SOURCE.is_file():
    raise SystemExit(f'patch-native-ufs-adopt-fix: missing {SOURCE}')

text = SOURCE.read_bytes().decode('utf-8', 'surrogateescape')
# undo the earlier, mis-targeted edit so the source matches upstream again
if WRONG in text:
    text = text.replace(WRONG, '  cacheClean (virt, 32768);', 1)
if FIXED in text:
    SOURCE.write_bytes(text.encode('utf-8', 'surrogateescape'))
    print('== patch-native-ufs-adopt-fix: already applied')
    raise SystemExit(0)
if BROKEN not in text:
    raise SystemExit('patch-native-ufs-adopt-fix: anchor not found; driver changed')

SOURCE.write_bytes(text.replace(BROKEN, FIXED, 1).encode('utf-8', 'surrogateescape'))
print('== patch-native-ufs-adopt-fix: applied at the adopt sequence')