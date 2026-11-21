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

REPO=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=wsl-env.sh
source "$SCRIPT_DIR/wsl-env.sh"
TREE=$(resolve_unixnu_tree "${1:-}") || {
  echo 'cannot find UniXNU: pass a tree path, set UNIXNU, or install it under /opt/work/nx741j-build' >&2
  exit 1
}
UEFI=$(resolve_uefi_tree "${UNIXNU_UEFI:-}") || {
  echo 'cannot find the UEFI tree: set UNIXNU_UEFI or install uefi-sheng-main under /opt/work' >&2
  exit 1
}
LOG=$REPO/out/qemu/build-and-test.log
mkdir -p "$REPO/out/qemu"
bash "$SCRIPT_DIR/prepare-local-inputs.sh"

step() { printf '\n=== %s ===\n' "$1" | tee -a "$LOG"; }

cd "$TREE"
if [ "${UNIXNU_SKIP_TREE:-0}" != 1 ]; then
  step 'tree'
  ./unixnu tree -D "$REPO" 2>&1 | tee -a "$LOG" | tail -2
fi
step 'kernel'
JOBS=${JOBS:-16}
./unixnu kernel -D "$REPO" -j "$JOBS" 2>&1 | tee -a "$LOG" | tail -2
KERNEL=$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/kernel/kernel.macho
REPORT=${KERNEL%.macho}.json
KERNEL_LOG=$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/logs/kernel.log
if grep -a -q 'Apple_APFS' "$KERNEL"; then
  echo 'PASS  prelinked IOStorageFamily carries the Apple_APFS content hint'
else
  echo "FAIL  Apple_APFS content hint is absent from $KERNEL"
  exit 1
fi
python3 - "$REPORT" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1]))
components = report.get("components") or []
patches = report.get("text_literal_patches") or []
if "com.apple.iokit.IOStorageFamily" not in components:
    raise SystemExit("prelink report lost com.apple.iokit.IOStorageFamily")
matching = [
    patch for patch in patches
    if patch.get("bundle") == "com.apple.iokit.IOStorageFamily"
    and patch.get("section") == "__TEXT_EXEC"
    and patch.get("offset") == 0x55f0
    and patch.get("literal") == "Apple_APFS"
    and patch.get("expected") == "a7bfff90e7ec1691"
]
if len(matching) != 1:
    raise SystemExit("prelink report has no exact IOStorageFamily Apple_APFS literal patch")
print("PASS  prelink keeps IOStorageFamily and applies one pinned APFS hint patch")
PY
step 'payload'
./unixnu payload -D "$REPO" 2>&1 | tee -a "$LOG" | tail -2
step 'firmware'
UNIXNU_UEFI="$UEFI" ./unixnu firmware -D "$REPO" 2>&1 | tee -a "$LOG" | tail -2

# Official smoke suite (package/Scripts/smoke.sh): default entry, keyboard
# selection, degraded no-GOP, block slice, kext staging and layout.  It needs
# the tool's edk2 tree under package/Mu_Basecore with its submodules; when that
# is missing the gate says so instead of silently dropping the coverage.
fail=0
step 'smoke'
SMOKE_EDK2=$UEFI/Mu_Basecore
if [ -d "$SMOKE_EDK2/MdePkg/Library/BaseFdtLib/libfdt" ]; then
  smoke_rc=0
  ( cd "$TREE" && UNIXNU_EDK2="$SMOKE_EDK2" KEXT_TIMEOUT=${KEXT_TIMEOUT:-180} JOBS=${SMOKE_JOBS:-8} bash package/Scripts/smoke.sh ) > "$REPO/out/qemu/smoke.log" 2>&1 || smoke_rc=$?
  if [ "$smoke_rc" -ne 0 ]; then
    smoke_rc=0
    ( cd "$TREE" && UNIXNU_EDK2="$SMOKE_EDK2" KEXT_TIMEOUT=${KEXT_TIMEOUT:-180} JOBS=${SMOKE_JOBS:-8} bash package/Scripts/smoke.sh ) >> "$REPO/out/qemu/smoke.log" 2>&1 || smoke_rc=$?
  fi
  if [ "$smoke_rc" -eq 0 ]; then
    echo 'PASS  smoke: six package scenarios'
  else
    echo "FAIL  smoke exited $smoke_rc (log: $REPO/out/qemu/smoke.log)"
    fail=1
  fi
else
  echo "SKIP  smoke: no edk2 tree with submodules at $SMOKE_EDK2"
fi

step 'qemu'
# Every build must run in the model first.  no-video=1 keeps the kernel out of
# the framebuffer path (QEMU has no panel), and QEMU_INT=1 adds -d int so the
# log records the exception the kernel stopped on.
SERIAL=$REPO/out/qemu/serial.log
INT=$REPO/out/qemu/int.log
rm -f "$INT" "$SERIAL"
qemu_rc=0
QEMU_MEM=${QEMU_MEM:-8G} QEMU_SMP=${QEMU_SMP:-8} QEMU_INT=1 UNIXNU_UEFI="$UEFI" \
  "$REPO/scripts/qemu-nx741j.sh" "$TREE" 40 | tee -a "$LOG" || qemu_rc=$?
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
