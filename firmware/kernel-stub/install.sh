#!/bin/bash
# Install KernelStubDxe into the nx741j firmware tree and wire it before SmemStubDxe.
set -eu
TREE=/opt/work/uefi-sheng-main
PKG="$TREE/Platforms/ZTE/nx741jPkg"
SRC=/mnt/e/default-workspace/z80u-collect/repos/nx741j/firmware/kernel-stub

mkdir -p "$PKG/Drivers/KernelStubDxe"
cp "$SRC/KernelStubDxe.c" "$SRC/KernelStubDxe.inf" "$PKG/Drivers/KernelStubDxe/"
chmod 644 "$PKG/Drivers/KernelStubDxe/"*

python3 - "$PKG" <<'PY'
import pathlib, sys

pkg = pathlib.Path(sys.argv[1])
stub = 'INF nx741jPkg/Drivers/KernelStubDxe/KernelStubDxe.inf'
anchor = 'INF nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf'

for name in ('DXE.inc', 'APRIORI.inc'):
    p = pkg / 'Include' / name
    lines = p.read_text().splitlines()
    if any(l.strip() == stub for l in lines):
        print(f'{name}: already patched')
        continue
    out = []
    done = False
    for ln in lines:
        if not done and ln.strip() == anchor:
            out.append('  ' + stub)
            done = True
        out.append(ln)
    if not done:
        raise SystemExit(f'{name}: anchor not found')
    p.write_text('\n'.join(out) + '\n')
    print(f'{name}: patched')

dsc = pkg / 'nx741j.dsc'
t = dsc.read_text()
line = '  nx741jPkg/Drivers/KernelStubDxe/KernelStubDxe.inf'
if line in t:
    print('dsc: already present')
else:
    a = '  nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf'
    if a not in t:
        raise SystemExit('dsc: anchor not found')
    t = t.replace(a, line + '\n' + a)
    dsc.write_text(t)
    print('dsc: added')
PY

echo '--- DXE.inc Kernel/Smem lines ---'
grep -n 'KernelStubDxe\|SmemStubDxe' "$PKG/Include/DXE.inc" || true
echo '--- APRIORI.inc Kernel/Smem lines ---'
grep -n 'KernelStubDxe\|SmemStubDxe' "$PKG/Include/APRIORI.inc" || true
echo '--- DSC components ---'
grep -n 'KernelStubDxe\|SmemStubDxe' "$PKG/nx741j.dsc" || true
