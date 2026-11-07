#!/bin/bash
set -eu
cd /mnt/e/default-workspace/z80u-collect/efi-loader
clang --target=aarch64-unknown-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector \
      -fshort-wchar -O2 -Wall -c p5.c -o p5.obj
lld-link /entry:EfiMain /subsystem:efi_application /nodefaultlib /machine:arm64 \
         /dynamicbase /fixed:no /debug:none /out:p5.efi p5.obj
python3 /mnt/e/default-workspace/z80u-collect/_tmp/pack_efisp.py \
    /mnt/e/default-workspace/z80u-collect/efi-loader/p5.efi \
    /mnt/e/default-workspace/z80u-collect/efi-loader/efisp-p5.img
