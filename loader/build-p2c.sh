#!/bin/bash
set -eu
cd /mnt/e/default-workspace/z80u-collect/efi-loader
clang --target=aarch64-unknown-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector \
      -fshort-wchar -O2 -Wall -c p2c.c -o p2c.obj
lld-link /entry:EfiMain /subsystem:efi_application /nodefaultlib /machine:arm64 \
         /dynamicbase /fixed:no /debug:none /out:p2c.efi p2c.obj
python3 - <<'PY'
import hashlib, struct
from pathlib import Path
p = Path('/mnt/e/default-workspace/z80u-collect/efi-loader/p2c.efi')
d = p.read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]
machine, nsec = struct.unpack_from('<HH', d, pe+4)
subsys = struct.unpack_from('<H', d, pe+24+68)[0]
print(f'p2c.efi: {len(d)} B machine=0x{machine:x} sections={nsec} subsystem={subsys} sha256={hashlib.sha256(d).hexdigest()[:16]}')
PY
python3 /mnt/e/default-workspace/z80u-collect/_tmp/pack_efisp.py \
    /mnt/e/default-workspace/z80u-collect/efi-loader/p2c.efi \
    /mnt/e/default-workspace/z80u-collect/efi-loader/efisp-p2c.img
