#!/bin/bash
# Build the proven NX741J loader with the host AArch64 LLVM tools.
#
# PAYLOAD=file build-posix.sh out-dir
#
# The output Image is the Android boot kernel payload. loader-boot.img is the
# matching Android v4 image with no ramdisk.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-out-proven}
PAYLOAD=${PAYLOAD:-$OUT/payload.bin}
CLANG=${CLANG:-/usr/bin/clang}
LD=${LD:-/usr/bin/ld.lld}
OBJCOPY=${OBJCOPY:-/usr/bin/llvm-objcopy}
READELF=${READELF:-/usr/bin/llvm-readelf}

cd "$HERE"
mkdir -p "$OUT"
if [[ $(realpath "$PAYLOAD") != $(realpath -m "$OUT/payload.bin") ]]; then
	cp -f "$PAYLOAD" "$OUT/payload.bin"
fi
[[ -s $OUT/payload.bin ]] || { echo "missing $OUT/payload.bin" >&2; exit 1; }
[[ -f $OUT/logo.bin ]] || : >"$OUT/logo.bin"

FLAGS=(
	--target=aarch64-none-elf -O2 -ffreestanding -fno-builtin -nostdlib
	-fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align
	-Wall -Wextra -Werror
)
"$CLANG" "${FLAGS[@]}" -c start.S -o "$OUT/start.o"
"$CLANG" "${FLAGS[@]}" -DPAYLOAD=\"$OUT/payload.bin\" -c payload.S -o "$OUT/payload.o"
"$CLANG" "${FLAGS[@]}" -DLOGO=\"$OUT/logo.bin\" -c logo.S -o "$OUT/logo.o"
"$CLANG" "${FLAGS[@]}" -c loader.c -o "$OUT/loader.o"

if "$READELF" -r "$OUT/loader.o" | grep -q R_AARCH64_ABS; then
	echo "absolute relocation in loader.c" >&2
	exit 1
fi
"$LD" -T loader.ld --build-id=none \
	"$OUT/start.o" "$OUT/loader.o" "$OUT/logo.o" "$OUT/payload.o" \
	-o "$OUT/loader.elf"
"$OBJCOPY" -O binary "$OUT/loader.elf" "$OUT/Image"

/usr/bin/python3 - "$OUT/Image" "$OUT/payload.bin" <<'PY'
import pathlib
import struct
import sys

image = pathlib.Path(sys.argv[1]).read_bytes()
payload = pathlib.Path(sys.argv[2]).read_bytes()
image_size = struct.unpack_from("<Q", image, 16)[0]
assert image[0x38:0x3C] == b"ARM\x64", "no arm64 Image magic"
assert image_size == len(image), (hex(image_size), hex(len(image)))
offset = image.find(payload[:64])
assert offset > 0 and offset % 4096 == 0, hex(offset)
assert image[offset : offset + len(payload)] == payload, "payload not embedded"
print(f"Image {len(image)} bytes, payload at {offset:#x} ({len(payload)} bytes)")
PY

/usr/bin/python3 ../../tools/mkbootimg_v4.py "$OUT/Image" "$OUT/loader-boot.img"
/usr/bin/python3 ../../tools/bootimg_info.py "$OUT/loader-boot.img"
sha256sum "$OUT/Image" "$OUT/payload.bin" "$OUT/loader-boot.img"
