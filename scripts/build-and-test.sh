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
if grep -q 'Synchronous Exception' "$SERIAL" 2>/dev/null; then
  echo 'FAIL  synchronous exception in the guest'
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  echo 'qemu gate failed: do not flash this build' >&2
  exit 1
fi
echo 'qemu gate passed: the payload reaches ExitBootServices and the kernel entry'
