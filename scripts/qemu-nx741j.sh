#!/bin/bash
# Boot the real payload (kernel + ADT + trust cache) through UniXNU and DarwinBoot
# inside QEMU, so a change can be tested before it touches the phone.
#
#   scripts/qemu-nx741j.sh <build-tree> [seconds]
#
# The ESP gets the DEBUG build of UniXNU.efi, the DarwinBoot driver as an
# autoloaded kit, and the Payloads/ directory taken from payload.img.  The
# machine is QEMU virt with 8 CPUs and 16 GiB so DarwinBoot can find its 256 MiB
# contiguous window; the serial log is the test output.
set -eu

TREE=${1:?usage: qemu-nx741j.sh <build-tree> [seconds]}
TIMEOUT=${2:-90}
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/out/qemu
UEFI=${UNIXNU_UEFI:-/opt/work/uefi-sheng-main}
DEV=$TREE/out/nx741j/ios-16.7.16-20H392/nx741j
ESP=$OUT/esp
LOG=$OUT/serial.log
# Truncate before the checks: if this script bails out early, the gate must see an
# empty log (no stale PASS from a previous run) rather than yesterday's markers.
: > "$LOG"

UNIXNU_EFI=$(ls "$UEFI"/Build/nx741jPkg/DEBUG_CLANGPDB/AARCH64/UniXNUPkg/Application/UniXNU/UniXNU/DEBUG/UniXNU.efi 2>/dev/null | head -1)
DARWIN_EFI=$(ls "$UEFI"/Build/nx741jPkg/DEBUG_CLANGPDB/AARCH64/DarwinBootPkg/Driver/DarwinBoot/DEBUG/DarwinBoot.efi 2>/dev/null | head -1)
[ -f "$UNIXNU_EFI" ] || { echo "missing UniXNU.efi under $UEFI" >&2; exit 1; }
[ -f "$DARWIN_EFI" ] || { echo "missing DarwinBoot.efi under $UEFI" >&2; exit 1; }
[ -f "$DEV/payload/payload.img" ] || { echo "missing payload.img under $DEV" >&2; exit 1; }

rm -rf "$ESP"; mkdir -p "$ESP/EFI/BOOT" "$ESP/EFI/UniXNU/Kits/DarwinBoot.kit" "$ESP/EFI/UniXNU/Payloads" "$OUT"
cp "$UNIXNU_EFI" "$ESP/EFI/BOOT/BOOTAA64.EFI"
cp "$DARWIN_EFI" "$ESP/EFI/UniXNU/Kits/DarwinBoot.kit/DarwinBoot.efi"
cat > "$ESP/EFI/UniXNU/Kits/DarwinBoot.kit/Kit.toml" <<'KIT'
schema = 1

[kit]
name = "DarwinBoot"
version = "1.0.0"
description = "Real DarwinBoot from the firmware build: Mach-O/ADT/trust cache checks, window, EBS, EL1 jump."

[match]
arch = "AARCH64"

[[driver]]
file = "DarwinBoot.efi"
KIT
cat > "$ESP/EFI/UniXNU/unixnu.toml" <<'CFG'
version = 1

[boot]
timeout = 3
default = "xnu"

[kits]
autoload = true

[[entry]]
id = "xnu"
title = "NX741J (QEMU model)"
type = "xnu"
path = "Payloads/kernel.macho"
args = "keepsyms=1 -v qemu-mark=1 no-video=1"
CFG

MNT=$(mktemp -d)
mount -o loop,ro "$DEV/payload/payload.img" "$MNT"
cp "$MNT/EFI/UniXNU/Payloads/kernel.macho" "$ESP/EFI/UniXNU/Payloads/"
cp "$MNT/EFI/UniXNU/Payloads/kernel.macho.adt" "$ESP/EFI/UniXNU/Payloads/"
cp "$MNT/EFI/UniXNU/Payloads/kernel.macho.trustcache" "$ESP/EFI/UniXNU/Payloads/"
umount "$MNT"; rmdir "$MNT"

# QEMU_ADT=fixture (default) follows the QEMU guide: the generated armvirt tree.
# QEMU_ADT=real feeds the phone's own 5,332-node tree instead, with only the
# DRAM window moved to QEMU RAM; every other address is the real one, so the
# kernel stops where the model has no device (scripts/mk-qemu-adt.py).
if [ "${QEMU_ADT:-fixture}" = "real" ]; then
  python3 "$REPO/scripts/mk-qemu-adt.py" --device "$REPO" \
    --out "$ESP/EFI/UniXNU/Payloads/kernel.macho.adt"
else
  python3 "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/tree/scripts/qemu-entry/make-dt.py" \
    --armvirt --armcpu --smp 8 --owned-memory-size 0x10000000 \
    --trust-cache --nvram-proxy \
    --output "$ESP/EFI/UniXNU/Payloads/kernel.macho.adt" >/dev/null
fi

FW=$(ls /usr/share/AAVMF/AAVMF_CODE.fd /usr/share/qemu-efi-aarch64/QEMU_EFI.fd 2>/dev/null | head -1)
[ -f "$FW" ] || { echo 'no AArch64 UEFI firmware found' >&2; exit 1; }

echo "esp: $ESP"
echo "firmware: $FW"
set +e
INT_ARGS=()
if [ "${QEMU_INT:-0}" = "1" ]; then
  INT_ARGS=(-d int -D "$OUT/int.log")
fi
timeout "$TIMEOUT" qemu-system-aarch64 \
  -M virt -cpu cortex-a72 -m 16G -smp 8 \
  -drive if=pflash,format=raw,readonly=on,file="$FW" \
  -drive format=raw,file=fat:rw:"$ESP" \
  -nographic -serial mon:stdio \
  "${INT_ARGS[@]}" \
  > "$LOG" 2>&1
STATUS=$?
set -e
echo "qemu exit: $STATUS (log: $LOG)"
echo '--- markers ---'
for m in 'UNIXNU: BootEntry' 'DARWINBOOT: Boot called' 'MARK #' 'EBS ok' 'ArmDTPlatform' 'panic'; do
  n=$(grep -c "$m" "$LOG" 2>/dev/null || true)
  printf '%-28s %s\n' "$m" "$n"
done
tail -20 "$LOG"

# A timeout is how a healthy run ends: the kernel keeps running in a machine
# model that has none of our hardware, so QEMU is killed instead of exiting.
# Only a crash before the hand-off counts as a runner failure.
if [ "$STATUS" -ne 0 ] && [ "$STATUS" -ne 124 ]; then
  exit "$STATUS"
fi
exit 0
