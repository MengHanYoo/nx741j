#!/usr/bin/env bash
# Reproduce the device-proven NX741J iOS 16 boot assets on the local WSL host.
#
# The base root must be the original-IPSW-built HFSX image documented in
# SWEET-ROOT-2026-11-22.md. iosroot-GOLDEN.img is never an input here.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
COLLECT=${COLLECT:-/mnt/e/default-workspace/z80u-collect}
LEGACY=${LEGACY:-$COLLECT/repos/nx741j}
OUT=${OUT:-$REPO/out/proven-route}
BUILD=${BUILD:-$OUT/build}
BASE_ROOT=${BASE_ROOT:-$COLLECT/iosroot-artifacts/iosroot-NX741J-20H392-20261122.img}
ROOT=${ROOT:-$COLLECT/iosroot-artifacts/iosroot-NX741J-20H392-proven-20261122.img}
EXTENTS=${EXTENTS:-$COLLECT/_tmp/nx741j-iosroot-extents-20261122.json}
IPSW_FIRMWARE=${IPSW_FIRMWARE:-/opt/work/ipsw/Firmware}
KERNEL=${KERNEL:-/opt/work/plk/k39-amfi.macho}
D22AP_ADT=${D22AP_ADT:-/opt/work/ipsw/d22ap.adt}
MOUNT=${MOUNT:-/opt/work/iosroot-proven-build}
USERDATA_FIRST=${USERDATA_FIRST:-4875048}
USERDATA_LAST=${USERDATA_LAST:-248838138}

TRUSTCACHE="$IPSW_FIRMWARE/087-86698-044.dmg.trustcache"
TRUSTCACHE+="+/opt/work/gpu/metal-out/plat/plugin.trst"
TRUSTCACHE+=" $IPSW_FIRMWARE/087-86593-047.dmg.trustcache"
TRUSTCACHE+="+$IPSW_FIRMWARE/087-86715-047.dmg.trustcache"

EXTRA="ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1"
EXTRA+=" wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1"
EXTRA+=" sweetfb_hz=30 sweetfb_fill=1 pandora_scanout=0"
EXTRA+=" pandora_touch_gpio117=0 pandora_gpu=14 pandora_gpu_metal=1"
EXTRA+=" sweetsample=MobileGestaltHel,containermanage,cfprefsd,lsd,SpringBoard"

[[ $EUID -eq 0 ]] || { echo "run as root in WSL" >&2; exit 1; }
for file in "$BASE_ROOT" "$EXTENTS" "$KERNEL" "$D22AP_ADT" \
	"$LEGACY/wsl/make_phone_payload_nx741j.sh" \
	"$LEGACY/shim/loader/build-posix.sh"; do
	[[ -f $file ]] || { echo "missing input: $file" >&2; exit 1; }
done
mkdir -p "$OUT" "$BUILD"

if ! grep -qw hfsplus /proc/filesystems; then
	insmod -f /opt/work/wslk-6.18.58.1/fs/hfsplus/hfsplus.ko
fi

if [[ ! -f $ROOT ]]; then
	echo "== copy original-IPSW root"
	cp --sparse=always "$BASE_ROOT" "$ROOT"
fi

check_known_hfs_state() {
	local output
	output=$(fsck.hfsplus -f -n "$ROOT" 2>&1) && return 0
	if grep -q 'Volume header needs minor repair' <<<"$output" &&
	   grep -q '^(1, 0)$' <<<"$output"; then
		return 0
	fi
	printf '%s\n' "$output" >&2
	return 1
}

check_known_hfs_state
mkdir -p "$MOUNT"
if mountpoint -q "$MOUNT"; then umount "$MOUNT"; fi
mount -t hfsplus -o loop,ro,force "$ROOT" "$MOUNT"
needs_patch=0
[[ -e $MOUNT/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache ]] ||
	needs_patch=1
[[ -e $MOUNT/System/Library/Extensions/NativeAdrenoMetal.bundle/NativeAdrenoMetal ]] ||
	needs_patch=1
[[ -e $MOUNT/private/var/fuxi/iosfw.img ]] || needs_patch=1
umount "$MOUNT"

if [[ $needs_patch == 1 ]]; then
	echo "== apply NX741J userland bring-up"
	mount -t hfsplus -o loop,rw,force "$ROOT" "$MOUNT"
	python3 "$REPO/scripts/apply-nx741j-root-bringup.py" "$MOUNT"
	python3 "$REPO/scripts/stage-proven-gpu.py" "$MOUNT"
	sync
	umount "$MOUNT"
	fsck.hfsplus -f -y "$ROOT"
fi
check_known_hfs_state

echo "== resolve staged firmware extents"
FW_RANGES=$OUT/iosfw-ranges.json
FW_EXTENTS=$OUT/iosfw-extents.json
IOSFW_INODE=$(fls -f hfs -r -p "$ROOT" | awk '/private\/var\/fuxi\/iosfw.img$/ {print $2; exit}' | tr -d ':')
[[ -n $IOSFW_INODE ]] || { echo "cannot find staged iofw inode" >&2; exit 1; }
python3 "$REPO/scripts/hfs-inode-ranges.py" \
	--snapshot "$ROOT" --extents "$EXTENTS" \
	--userdata-first-lba "$USERDATA_FIRST" \
	--inode "$IOSFW_INODE" --out "$FW_RANGES"
python3 "$REPO/scripts/hfs-file-to-pinfw.py" \
	--ranges "$FW_RANGES" --out "$FW_EXTENTS"

echo "== build SWEETXNU payload"
env KERNEL="$KERNEL" TRUSTCACHE="$TRUSTCACHE" \
	ADTFLAGS="--root-extents $EXTENTS --fw-extents $FW_EXTENTS" CPUS=8 RD=ufsroot0 DEBUG=0xc \
	NAME=proven-route D22AP_ADT="$D22AP_ADT" \
	bash "$LEGACY/wsl/make_phone_payload_nx741j.sh" /opt/work/run1 "$EXTRA"

echo "== build boot_a"
env PAYLOAD="$LEGACY/shim/loader/proven-route/payload.bin" \
	bash "$LEGACY/shim/loader/build-posix.sh" "$BUILD"
cp -f "$BUILD/loader-boot.img" "$OUT/loader-boot.img"
cp -f "$BUILD/payload.bin" "$OUT/payload.img"

echo "== static gate"
python3 "$REPO/scripts/check-proven-route.py" \
	--boot "$OUT/loader-boot.img" \
	--root "$ROOT" \
	--extents "$EXTENTS" \
	--fw-extents "$FW_EXTENTS" \
	--adt-tools "$LEGACY/tools" \
	--json-out "$OUT/boot-check.json"

echo "== root delta"
python3 "$REPO/scripts/diff-pinned-root.py" \
	--base "$BASE_ROOT" --target "$ROOT" --extents "$EXTENTS" \
	--userdata-first-lba "$USERDATA_FIRST" \
	--userdata-last-lba "$USERDATA_LAST" \
	--out "$OUT/root-delta.json"
rm -rf "$OUT/root-delta"
python3 "$REPO/scripts/extract-root-delta.py" \
	--report "$OUT/root-delta.json" --base "$BASE_ROOT" --target "$ROOT" \
	--out-dir "$OUT/root-delta"

echo "== ready"
sha256sum "$ROOT" "$OUT/loader-boot.img" "$OUT/payload.img" \
	"$OUT/boot-check.json" "$OUT/root-delta.json"
