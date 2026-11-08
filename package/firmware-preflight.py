#!/usr/bin/env python3
"""Board preflight (runs inside the assembled tree, no arguments).

1. mirror package/firmware-overlay/ onto the UEFI checkout;
2. copy the vendored UniXNUDxe.c (direct-start fallback) over the shared package copy.

The UEFI checkout is discovered from UNIXNU_UEFI, then /opt/work/uefi-sheng-main, then any
sibling /opt/work/* that contains Platforms/ZTE/nx741jPkg/nx741j.dsc.
"""
import os
import shutil
import sys
from pathlib import Path

tree = Path.cwd()
pkg = tree / 'kits' / 'nx741j'
overlay = pkg / 'firmware-overlay'
if not overlay.is_dir():
    sys.exit(f'preflight: overlay missing at {overlay}')

def find_uefi():
    env = os.environ.get('UNIXNU_UEFI')
    if env and Path(env).is_dir():
        return Path(env)
    candidates = [Path('/opt/work/uefi-sheng-main')]
    candidates += sorted(Path('/opt/work').glob('*/Platforms/ZTE/nx741jPkg/nx741j.dsc'))
    for c in candidates:
        p = c if c.is_dir() else c.parent.parent.parent
        if (p / 'Platforms/ZTE/nx741jPkg/nx741j.dsc').is_file():
            return p
    return None

uefi = find_uefi()
if uefi is None:
    sys.exit('preflight: UEFI checkout not found (set UNIXNU_UEFI)')

copied = 0
for src in overlay.rglob('*'):
    if not src.is_file():
        continue
    dst = uefi / src.relative_to(overlay)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    copied += 1

upstream = pkg / 'upstream/UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.c'
target = tree / 'package/UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.c'
if upstream.is_file() and target.parent.is_dir():
    shutil.copy2(upstream, target)
    print(f'preflight: vendored UniXNUDxe.c -> {target}')

print(f'preflight: overlay {copied} file(s) -> {uefi}')
