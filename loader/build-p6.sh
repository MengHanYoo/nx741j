#!/bin/bash
set -eu
R=/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
WORK=/tmp/p6-build
mkdir -p "$WORK"
clang --target=aarch64-unknown-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector \
      -fshort-wchar -O2 -Wall -c "$R/loader/p6.c" -o "$WORK/p6.obj"
lld-link /entry:EfiMain /subsystem:efi_application /nodefaultlib /machine:arm64 \
         /dynamicbase /fixed:no /debug:none /out:"$WORK/p6.efi" "$WORK/p6.obj"
python3 /mnt/e/default-workspace/z80u-collect/_tmp/pack_efisp.py "$WORK/p6.efi" "$R/loader/images/efisp-p6.img"
sha256sum "$R/loader/images/efisp-p6.img"
