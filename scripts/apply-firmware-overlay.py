#!/usr/bin/env python3
"""Copy the NX741J board overlay into the UEFI tree before a firmware build.

Usage: apply-firmware-overlay.py <uefi-tree>
The overlay mirrors the platform package layout, e.g.
package/firmware-overlay/Platforms/ZTE/nx741jPkg/... -> <uefi>/Platforms/ZTE/nx741jPkg/...
"""
import shutil
import sys
from pathlib import Path

overlay = Path(__file__).resolve().parent.parent / "package" / "firmware-overlay"
if len(sys.argv) != 2:
    sys.exit("usage: apply-firmware-overlay.py <uefi-tree>")
target = Path(sys.argv[1]).resolve()
if not overlay.is_dir():
    sys.exit(f"overlay missing: {overlay}")

copied = 0
for src in overlay.rglob("*"):
    if not src.is_file():
        continue
    rel = src.relative_to(overlay)
    dst = target / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    copied += 1
print(f"overlay applied: {copied} file(s) -> {target}")
