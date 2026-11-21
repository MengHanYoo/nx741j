#!/bin/bash
# Fast iteration build: kernel -> payload -> firmware, no QEMU smoke.
# Use scripts/build-and-test.sh for milestones that must pass the gate.
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
LOGDIR=$REPO/out/qemu
mkdir -p "$LOGDIR"
JOBS=${JOBS:-16}
cd "$TREE"
echo '=== kernel ==='
./unixnu kernel -D "$REPO" -j "$JOBS" 2>&1 | tee "$LOGDIR/fast-kernel.log" | tail -3
echo '=== payload ==='
./unixnu payload -D "$REPO" 2>&1 | tee "$LOGDIR/fast-payload.log" | tail -3
echo '=== firmware ==='
UNIXNU_UEFI="$UEFI" ./unixnu firmware -D "$REPO" 2>&1 | tee "$LOGDIR/fast-firmware.log" | tail -3
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img" "$ARTIFACTS/payload-fast.img"
cp -f "$UEFI/Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd" "$ARTIFACTS/fd-fast.bin"
sha256sum "$ARTIFACTS/payload-fast.img" "$ARTIFACTS/fd-fast.bin"
