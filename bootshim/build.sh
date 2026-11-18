#!/bin/bash
# Build the NX741J standalone loader (data-a-live shim/loader) and pack a boot.img.
set -euo pipefail
cd "$(dirname "$0")"
R=$(cd .. && pwd)
T=${UNIXNU_TOOLCHAIN:-/opt/work/unixnu-linux}/CommandLineTools/usr/bin
L=/usr/lib/llvm-21/bin
O=${O:-/opt/work/nx741j-build/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j}
export CROSS_EXTRA_FLAGS=
out=out
mkdir -p $out
K=$O/kernel/kernel.macho
ADT=$O/device-tree/hardware.dtb
test -f $K || { echo missing kernel; exit 1; }
test -f $ADT || { echo missing adt; exit 1; }
python3 $R/scripts/macho-to-sweet-payload.py --kernel "$K" --adt "$ADT" --phys-base 0x300000000 --mem-size 0x5000000 --cmdline "$(python3 $R/bootshim/cmdline.py $R/board.toml)" -o $out/payload.bin
: > $out/logo.bin
FLAGS="--target=aarch64-none-elf -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror"
$T/clang $FLAGS -c src/start.S -o $out/start.o
$T/clang $FLAGS -DPAYLOAD=\"$out/payload.bin\" -c src/payload.S -o $out/payload.o
$T/clang $FLAGS -DLOGO=\"$out/logo.bin\" -c src/logo.S -o $out/logo.o
$T/clang $FLAGS -c src/loader.c -o $out/loader.o
if $L/llvm-readelf -r $out/loader.o | grep -q R_AARCH64_ABS; then echo absolute relocation; exit 1; fi
$L/ld.lld -T src/loader.ld --build-id=none $out/start.o $out/loader.o $out/logo.o $out/payload.o -o $out/loader.elf
$L/llvm-objcopy -O binary $out/loader.elf $out/Image
python3 $R/bootshim/check_embed.py $out/Image $out/payload.bin
python3 -I tools/mkbootimg_v4.py $out/Image $out/boot.img
ls -l $out/Image $out/boot.img | awk '{print $5, $9}'
sha256sum $out/boot.img
