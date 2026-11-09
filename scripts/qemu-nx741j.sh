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
OUT=$(cd "$(dirname "$0")/.." && pwd)/out/qemu
UEFI=${UNIXNU_UEFI:-/opt/work/uefi-sheng-main}
DEV=$TREE/out/nx741j/ios-16.7.16-20H392/nx741j
ESP=$OUT/esp
LOG=$OUT/serial.log

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
args = "keepsyms=1 debug=0x144 -v serial=3 qemu-mark=1 scratch-video=1"
CFG

MNT=$(mktemp -d)
mount -o loop,ro "$DEV/payload/payload.img" "$MNT"
cp "$MNT/EFI/UniXNU/Payloads/kernel.macho" "$ESP/EFI/UniXNU/Payloads/"
cp "$MNT/EFI/UniXNU/Payloads/kernel.macho.adt" "$ESP/EFI/UniXNU/Payloads/"
cp "$MNT/EFI/UniXNU/Payloads/kernel.macho.trustcache" "$ESP/EFI/UniXNU/Payloads/"
umount "$MNT"; rmdir "$MNT"

FW=$(ls /usr/share/AAVMF/AAVMF_CODE.fd /usr/share/qemu-efi-aarch64/QEMU_EFI.fd 2>/dev/null | head -1)
[ -f "$FW" ] || { echo 'no AArch64 UEFI firmware found' >&2; exit 1; }

echo "esp: $ESP"
echo "firmware: $FW"
set +e
timeout "$TIMEOUT" qemu-system-aarch64 \
  -M virt -cpu cortex-a72 -m 16G -smp 8 \
  -drive if=pflash,format=raw,readonly=on,file="$FW" \
  -drive format=raw,file=fat:rw:"$ESP" \
  -nographic -serial mon:stdio \
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
