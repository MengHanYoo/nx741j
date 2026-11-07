#!/bin/bash
set -eu
cd /mnt/e/default-workspace/z80u-collect/efi-loader
clang --target=aarch64-unknown-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector \
      -fshort-wchar -O2 -Wall -Wextra -c p2.c -o p2.obj
lld-link /entry:EfiMain /subsystem:efi_application /nodefaultlib /machine:arm64 \
         /dynamicbase /fixed:no /debug:none /out:p2.efi p2.obj
ls -la p2.efi
file p2.efi
llvm-objdump -h p2.efi
python3 - <<'PY'
import hashlib, struct
from pathlib import Path
p = Path('/mnt/e/default-workspace/z80u-collect/efi-loader/p2.efi')
d = p.read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]
machine, nsec = struct.unpack_from('<HH', d, pe+4)
subsys = struct.unpack_from('<H', d, pe+24+68)[0]
print(f'p2.efi: {len(d)} B machine=0x{machine:x} sections={nsec} subsystem={subsys} sha256={hashlib.sha256(d).hexdigest()[:16]}')
PY
