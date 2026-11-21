#!/bin/bash
# Fast iteration chain: kernel -> payload -> firmware -> ONE QEMU boot-chain run.
# The full gate (scripts/build-and-test.sh) additionally runs the six smoke
# scenarios and the kext sub-cases; use it for milestones only.
set -eu
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
ARTIFACTS=$(resolve_artifact_dir "${UNIXNU_ARTIFACTS:-}")
TIMEOUT=${2:-25}
JOBS=${JOBS:-16}
export QEMU_MEM=${QEMU_MEM:-8G}
export QEMU_SMP=${QEMU_SMP:-8}
LOGDIR=$REPO/out/qemu
mkdir -p "$LOGDIR"
start=$(date +%s)
cd "$TREE"
echo '=== kernel ==='
./unixnu kernel -D "$REPO" -j "$JOBS" 2>&1 | tee "$LOGDIR/fast-kernel.log" | tail -2
echo '=== payload ==='
./unixnu payload -D "$REPO" 2>&1 | tee "$LOGDIR/fast-payload.log" | tail -2
echo '=== firmware ==='
UNIXNU_UEFI="$UEFI" ./unixnu firmware -D "$REPO" 2>&1 | tee "$LOGDIR/fast-firmware.log" | tail -2
build_done=$(date +%s)
echo "=== build: $((build_done - start))s ==="
echo '=== qemu boot chain ==='
UNIXNU_UEFI="$UEFI" bash "$REPO/scripts/qemu-nx741j.sh" "$TREE" "$TIMEOUT" > "$LOGDIR/fast-qemu.log" 2>&1 || true
grep -E 'UNIXNU: BootEntry|MARK #29|QEMU: EBS ok|Synchronous' "$LOGDIR/fast-qemu.log" | tail -5
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img" "$ARTIFACTS/payload-fast.img"
cp -f "$UEFI/Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd" "$ARTIFACTS/fd-fast.bin"
echo "=== total: $(( $(date +%s) - start ))s ==="
sha256sum "$ARTIFACTS/payload-fast.img" "$ARTIFACTS/fd-fast.bin"
