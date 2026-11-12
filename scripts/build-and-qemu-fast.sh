#!/bin/bash
# Fast iteration chain: kernel -> payload -> firmware -> ONE QEMU boot-chain run.
# The full gate (scripts/build-and-test.sh) additionally runs the six smoke
# scenarios and the kext sub-cases; use it for milestones only.
set -eu
TREE=${1:-/home/loongko/nx741j-build/UniXNU}
TIMEOUT=${2:-25}
JOBS=${JOBS:-16}
export QEMU_MEM=${QEMU_MEM:-8G}
export QEMU_SMP=${QEMU_SMP:-4}
REPO=$(cd "$(dirname "$0")/.." && pwd)
LOGDIR=$REPO/out/qemu
mkdir -p "$LOGDIR"
start=$(date +%s)
cd "$TREE"
echo '=== kernel ==='
./unixnu kernel -D "$REPO" -j "$JOBS" 2>&1 | tee "$LOGDIR/fast-kernel.log" | tail -2
echo '=== payload ==='
./unixnu payload -D "$REPO" 2>&1 | tee "$LOGDIR/fast-payload.log" | tail -2
echo '=== firmware ==='
./unixnu firmware -D "$REPO" --uefi /opt/work/uefi-sheng-main 2>&1 | tee "$LOGDIR/fast-firmware.log" | tail -2
build_done=$(date +%s)
echo "=== build: $((build_done - start))s ==="
echo '=== qemu boot chain ==='
bash "$REPO/scripts/qemu-nx741j.sh" "$TREE" "$TIMEOUT" > "$LOGDIR/fast-qemu.log" 2>&1 || true
grep -E 'UNIXNU: BootEntry|MARK #29|QEMU: EBS ok|Synchronous' "$LOGDIR/fast-qemu.log" | tail -5
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img" /mnt/e/default-workspace/z80u-collect/_tmp/payload-fast.img
cp -f /opt/work/uefi-sheng-main/Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd /mnt/e/default-workspace/z80u-collect/_tmp/fd-fast.bin
echo "=== total: $(( $(date +%s) - start ))s ==="
sha256sum /mnt/e/default-workspace/z80u-collect/_tmp/payload-fast.img /mnt/e/default-workspace/z80u-collect/_tmp/fd-fast.bin
