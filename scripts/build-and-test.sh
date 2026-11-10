#!/bin/bash
# Build the images and prove them in QEMU before anything touches the phone.
#
#   sudo scripts/build-and-test.sh [build-tree]
#
# Steps: kernel (incremental) -> payload -> firmware -> QEMU run with the real
# payload. Exits non-zero if a marker is missing, so a broken build never
# reaches 9008.
set -eu
# A failing QEMU step must fail the gate, not be swallowed by the pipeline below.
set -o pipefail

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

# Official smoke suite (package/Scripts/smoke.sh): default entry, keyboard
# selection, degraded no-GOP, block slice, kext staging and layout.  It needs
# the tool's edk2 tree under package/Mu_Basecore with its submodules; when that
# is missing the gate says so instead of silently dropping the coverage.
step 'smoke'
if [ -d "$TREE/package/Mu_Basecore/MdePkg/Library/BaseFdtLib/libfdt" ]; then
  smoke_rc=0
  ( cd "$TREE" && JOBS=8 bash package/Scripts/smoke.sh ) > "$REPO/out/qemu/smoke.log" 2>&1 || smoke_rc=$?
  if [ "$smoke_rc" -eq 0 ]; then
    echo 'PASS  smoke: six package scenarios'
  else
    echo "FAIL  smoke exited $smoke_rc (log: $REPO/out/qemu/smoke.log)"
    fail=1
  fi
else
  echo "SKIP  smoke: no edk2 tree with submodules at $TREE/package/Mu_Basecore"
fi

step 'qemu'
# Every build must run in the model first.  no-video=1 keeps the kernel out of
# the framebuffer path (QEMU has no panel), and QEMU_INT=1 adds -d int so the
# log records the exception the kernel stopped on.
SERIAL=$REPO/out/qemu/serial.log
INT=$REPO/out/qemu/int.log
rm -f "$INT" "$SERIAL"
qemu_rc=0
QEMU_INT=1 "$REPO/scripts/qemu-nx741j.sh" "$TREE" 40 | tee -a "$LOG" || qemu_rc=$?
if [ -s "$INT" ]; then
  python3 - "$INT" <<'PY' | tee -a "$LOG" || true
import sys
try:
    lines = open(sys.argv[1], errors="ignore").read().splitlines()
except OSError:
    sys.exit(0)
keep = [l for l in lines if l.startswith("Taking exception") or l.startswith("...with")]
if keep:
    print("MODEL kernel stopped at " + " | ".join(keep[-3:]))
PY
fi

fail=0
# The gate only means something if QEMU really booted the payload: the runner
# truncates serial.log before its own checks, so a bail-out leaves an empty log,
# and the firmware banner below can only come from a guest that actually ran.
if [ "$qemu_rc" -ne 0 ]; then
  echo "FAIL  qemu runner exited $qemu_rc (it never booted the payload)"
  fail=1
fi
if ! grep -q 'UEFI firmware (version' "$SERIAL" 2>/dev/null; then
  echo "FAIL  no guest firmware banner in $SERIAL (QEMU did not run the payload)"
  fail=1
fi
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
ebs_line=$(grep -n 'QEMU: EBS ok, entering the kernel' "$SERIAL" 2>/dev/null | head -1 | cut -d: -f1 || true)
exc_line=$(grep -n 'Synchronous Exception' "$SERIAL" 2>/dev/null | head -1 | cut -d: -f1 || true)
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
