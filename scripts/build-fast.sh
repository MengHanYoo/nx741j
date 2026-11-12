#!/bin/bash
# Fast iteration build: kernel -> payload -> firmware, no QEMU smoke.
# Use scripts/build-and-test.sh for milestones that must pass the gate.
set -eu
TREE=${1:-/home/loongko/nx741j-build/UniXNU}
REPO=$(cd "$(dirname "$0")/.." && pwd)
LOGDIR=$REPO/out/qemu
mkdir -p "$LOGDIR"
JOBS=${JOBS:-16}
cd "$TREE"
echo '=== kernel ==='
./unixnu kernel -D "$REPO" -j "$JOBS" 2>&1 | tee "$LOGDIR/fast-kernel.log" | tail -3
echo '=== payload ==='
./unixnu payload -D "$REPO" 2>&1 | tee "$LOGDIR/fast-payload.log" | tail -3
echo '=== firmware ==='
./unixnu firmware -D "$REPO" --uefi /opt/work/uefi-sheng-main 2>&1 | tee "$LOGDIR/fast-firmware.log" | tail -3
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img" /mnt/e/default-workspace/z80u-collect/_tmp/payload-fast.img
cp -f /opt/work/uefi-sheng-main/Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd /mnt/e/default-workspace/z80u-collect/_tmp/fd-fast.bin
sha256sum /mnt/e/default-workspace/z80u-collect/_tmp/payload-fast.img /mnt/e/default-workspace/z80u-collect/_tmp/fd-fast.bin
