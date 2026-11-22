#!/usr/bin/env bash
# Boot the P7 loader through AAVMF, make it read a sparse 4K-GPT rawdump,
# then jump into the same FD used by the phone. Host-only MMIO diagnostics are
# compiled out; GPT, payload/FD reads, copy, cache transition and branch remain.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-"$REPO/out/qemu-loader"}
TIMEOUT=${2:-45}
QEMU_MEM=${QEMU_MEM:-16G}
QEMU_SMP=${QEMU_SMP:-16}
BUILD=$OUT/build
ESP=$OUT/esp
DISK=$OUT/rawdump.img
LOG=$OUT/serial.log
DEBUG_LOG=$OUT/qemu-debug.log

mkdir -p "$BUILD" "$ESP/EFI/BOOT"
rm -f "$DISK" "$LOG" "$DEBUG_LOG"

clang --target=aarch64-unknown-windows-msvc -ffreestanding -fno-builtin \
  -fno-stack-protector -fshort-wchar -O2 -Wall -DP7_QEMU_TEST \
  -c "$REPO/loader/p7.c" -o "$BUILD/p7-qemu.obj"
lld-link /entry:EfiMain /subsystem:efi_application /nodefaultlib /machine:arm64 \
  /dynamicbase /fixed:no /debug:none /out:"$BUILD/p7-qemu.efi" "$BUILD/p7-qemu.obj"

python3 "$REPO/scripts/make-loader-gpt.py" \
  --disk "$DISK" \
  --payload "$REPO/out/final-sweet-root/payload.img" \
  --fd "$REPO/out/final-sweet-root/SILICIUM_UEFI.fd"

cp -f "$BUILD/p7-qemu.efi" "$ESP/EFI/BOOT/BOOTAA64.EFI"
FW=$(ls /usr/share/AAVMF/AAVMF_CODE.fd /usr/share/qemu-efi-aarch64/QEMU_EFI.fd 2>/dev/null | head -1)
[ -f "$FW" ] || { echo "no AArch64 UEFI firmware" >&2; exit 1; }

set +e
DEBUG_ARGS=()
if [ "${QEMU_DEBUG:-0}" = "1" ]; then
  DEBUG_EVENTS=int,guest_errors,cpu_reset
  if [ -n "${QEMU_DFILTER:-}" ]; then
    DEBUG_EVENTS=$DEBUG_EVENTS,in_asm
    DEBUG_ARGS+=(-dfilter "$QEMU_DFILTER")
  fi
  DEBUG_ARGS+=(-d "$DEBUG_EVENTS" -D "$DEBUG_LOG")
fi
if [ "${QEMU_GDB:-0}" = "1" ]; then
  DEBUG_ARGS+=(-S -gdb "tcp:127.0.0.1:${QEMU_GDB_PORT:-1234}")
fi
timeout --foreground --kill-after=5s "${TIMEOUT}s" qemu-system-aarch64 \
  -M virt -cpu cortex-a72 -m "$QEMU_MEM" -smp "$QEMU_SMP" \
  -drive if=pflash,format=raw,readonly=on,file="$FW" \
  -drive if=none,format=raw,file="fat:rw:$ESP",id=esp \
  -device virtio-blk-pci,drive=esp,bootindex=0 \
  -drive if=none,format=raw,file="$DISK",id=rawdump \
  -device virtio-blk-pci,drive=rawdump,logical_block_size=4096,physical_block_size=4096,bootindex=1 \
  -display none -serial "file:$LOG" -monitor none -no-reboot \
  "${DEBUG_ARGS[@]}" \
  >"$OUT/qemu.stderr" 2>&1
STATUS=$?
set -e

python3 - "$DISK" <<'PY'
import struct
import sys
from pathlib import Path

sector = 4096
with Path(sys.argv[1]).open("rb", buffering=0) as disk:
    for lba in (4764200, 4764201):
        disk.seek(lba * sector)
        magic, step, status = struct.unpack("<IIQ", disk.read(16))
        print(f"LBA {lba} magic={magic:08x} step={step} status={status:016x}")
PY

echo "qemu exit: $STATUS (log: $LOG)"
for marker in 'UNIXNU: BootEntry' 'DARWINBOOT: p13' 'EBS ok' 'panic'; do
  count=$(grep -c "$marker" "$LOG" 2>/dev/null || true)
  printf '%-28s %s\n' "$marker" "$count"
done
tail -30 "$LOG" 2>/dev/null || true

if [ "$STATUS" -ne 0 ] && [ "$STATUS" -ne 124 ] && [ "$STATUS" -ne 137 ]; then
  exit "$STATUS"
fi
