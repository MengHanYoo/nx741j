#!/bin/bash
# Build the images and prove them in QEMU before anything touches the phone.
#
#   sudo scripts/build-and-test.sh [build-tree]
#
# Steps: kernel (incremental) -> payload -> firmware -> QEMU run with the real
# payload. Exits non-zero if a marker is missing, so a broken build never
# reaches 9008.
set -eu

TREE=${1:-/home/loongko/nx741j-build/UniXNU}
REPO=$(cd "$(dirname "$0")/.." && pwd)
LOG=$REPO/out/qemu/build-and-test.log
mkdir -p "$REPO/out/qemu"

step() { printf '\n=== %s ===\n' "$1" | tee -a "$LOG"; }

cd "$TREE"
step 'kernel'
./unixnu kernel -D "$REPO" -j 16 2>&1 | tee -a "$LOG" | tail -2
step 'payload'
./unixnu payload -D "$REPO" 2>&1 | tee -a "$LOG" | tail -2
step 'firmware'
UNIXNU_UEFI=${UNIXNU_UEFI:-/opt/work/uefi-sheng-main} ./unixnu firmware -D "$REPO" 2>&1 | tee -a "$LOG" | tail -2

step 'qemu'
"$REPO/scripts/qemu-nx741j.sh" "$TREE" 40 | tee -a "$LOG"

SERIAL=$REPO/out/qemu/serial.log
fail=0
for marker in 'UNIXNU: BootEntry' 'MARK #29' 'QEMU: EBS ok, entering the kernel'; do
  if grep -q "$marker" "$SERIAL" 2>/dev/null; then
    printf 'PASS  %s\n' "$marker"
  else
    printf 'FAIL  %s\n' "$marker"
    fail=1
  fi
done
# A guest exception before the hand-off is ours (firmware or DarwinBoot) and
# blocks the build. One after it is the kernel running in a machine model that
# has none of our hardware, so it is reported but does not block.
ebs_line=$(grep -n 'QEMU: EBS ok, entering the kernel' "$SERIAL" 2>/dev/null | head -1 | cut -d: -f1)
exc_line=$(grep -n 'Synchronous Exception' "$SERIAL" 2>/dev/null | head -1 | cut -d: -f1)
if [ -n "$exc_line" ]; then
  if [ -z "$ebs_line" ] || [ "$exc_line" -lt "$ebs_line" ]; then
    echo "FAIL  synchronous exception at line $exc_line (before the hand-off)"
    fail=1
  else
    echo "NOTE  guest exception at line $exc_line happens after the hand-off: the kernel ran into the QEMU machine model, not a loader bug"
  fi
fi

if [ "$fail" -ne 0 ]; then
  echo 'qemu gate failed: do not flash this build' >&2
  exit 1
fi
echo 'qemu gate passed: the payload reaches ExitBootServices and the kernel entry'
