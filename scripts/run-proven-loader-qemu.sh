#!/usr/bin/env bash
# Run the historical shim-loader chain under QEMU, then copy evidence to E:.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
LEGACY=/mnt/e/default-workspace/z80u-collect/repos/nx741j
BOOT_OUT=$LEGACY/shim/loader/out-proven-route
EVIDENCE=$REPO/out/proven-route/qemu-loader
WORK=/opt/work/proven-loader-qemu
SECS=${1:-90}
KERNEL=${KERNEL:-/opt/work/plk/k39-amfi.macho}

[[ -f $BOOT_OUT/Image && -f $BOOT_OUT/payload.bin ]] || {
	echo "missing proven loader image/payload" >&2
	exit 1
}

old_overcommit=$(cat /proc/sys/vm/overcommit_memory)
cleanup() {
	rm -f "$WORK/mon.sock"
	echo "$old_overcommit" >/proc/sys/vm/overcommit_memory
	mkdir -p "$EVIDENCE"
	cp -af "$WORK"/. "$EVIDENCE"/
}
trap cleanup EXIT
echo 1 >/proc/sys/vm/overcommit_memory
rm -rf "$WORK"
mkdir -p "$WORK"

python3 "$LEGACY/tools/qemu_loader.py" \
	"$BOOT_OUT/Image" "$BOOT_OUT/payload.bin" "$WORK" \
	--secs "$SECS" --kernel "$KERNEL"

echo "== framebuffer text =="
grep -v '^[[:space:]]*$' "$WORK/fb.txt" | tail -80 || true
echo "== registers =="
sed -n '1,100p' "$WORK/regs.txt" || true
