#!/bin/bash
# Install SmemStubDxe into the nx741j firmware tree and wire it into DXE/APRIORI.
set -eu
TREE=/opt/work/uefi-sheng-main
PKG="$TREE/Platforms/ZTE/nx741jPkg"
SRC=/mnt/e/default-workspace/z80u-collect/repos/nx741j/firmware/smem-stub

mkdir -p "$PKG/Drivers/SmemStubDxe"
cp "$SRC/SmemStubDxe.c" "$SRC/SmemStubDxe.inf" "$PKG/Drivers/SmemStubDxe/"
chmod 644 "$PKG/Drivers/SmemStubDxe/"*

python3 - "$PKG" <<'PY'
import sys, pathlib
pkg = pathlib.Path(sys.argv[1])
stub = 'INF nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf'
real = 'INF Binaries/nx741j/QcomPkg/Drivers/SmemDxe/SmemDxe.inf'

for name in ('DXE.inc', 'APRIORI.inc'):
    p = pkg / 'Include' / name
    lines = p.read_text().splitlines()
    if any(l.strip() == stub for l in lines):
        print(f'{name}: already patched, skip')
        continue
    out = []
    changed = False
    for ln in lines:
        if ln.strip() == real:
            out.append('  ' + stub)
            out.append('#  ' + real + '  # replaced by SmemStubDxe')
            changed = True
        else:
            out.append(ln)
    if not changed:
        raise SystemExit(f'{name}: SmemDxe line not found')
    p.write_text('\n'.join(out) + '\n')
    print(f'{name}: patched')
PY

echo '--- DXE.inc SMEM lines ---'
grep -n 'Smem' "$PKG/Include/DXE.inc" || true
echo '--- APRIORI.inc SMEM lines ---'
grep -n 'Smem' "$PKG/Include/APRIORI.inc" || true
echo '--- DSC Components ---'
python3 - "$PKG" <<'PY2'
import pathlib, sys
p = pathlib.Path(sys.argv[1]) / 'nx741j.dsc'
t = p.read_text()
line = '  nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf'
if line in t:
    print('dsc: already present')
else:
    t = t.replace('  nx741jPkg/Drivers/SmemDumpDxe/SmemDumpDxe.inf', line + '\n  nx741jPkg/Drivers/SmemDumpDxe/SmemDumpDxe.inf')
    p.write_text(t)
    print('dsc: added')
PY2

echo '--- driver dir ---'
ls -l "$PKG/Drivers/SmemStubDxe"

