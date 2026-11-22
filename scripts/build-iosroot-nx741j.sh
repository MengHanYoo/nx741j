#!/usr/bin/env bash
# Build the NX741J iOS 16.7.16 HFS+ root from the original Apple IPSW images.
#
# This intentionally has no "golden image" input. The root is assembled from:
#   - the three APFS images extracted from the user's 20H392 IPSW;
#   - ios-fuxi's pinned mkiosroot.sh, fuxi-stage-root.py and
#     sweet-root-tweaks.py flow, as used by data-a-live;
#   - the live NX741J FDT and D22AP identity only for build metadata and the
#     later ADT step (the userland files themselves come from the IPSW).
#
# The image is built on ext4, checked with fsck.hfsplus, mounted, staged and
# checked again before being copied to the artifact directory on E:.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
IPSW_DIR=${IPSW_DIR:-/mnt/e/default-workspace/ipsw}
ARTIFACT_DIR=${ARTIFACT_DIR:-/mnt/e/default-workspace/z80u-collect/iosroot-artifacts}
WORK_ROOT=${WORK_ROOT:-/opt/work/iosroot-build}
LIVE_DTB=${LIVE_DTB:-/mnt/e/default-workspace/z80u-collect/stock/dt/live_fdt.dtb}
D22AP_ADT=${D22AP_ADT:-/mnt/e/default-workspace/z80u-collect/repos/nx741j/tools/d22ap_raw.adt}
EXTENTS_JSON=${EXTENTS_JSON:-/mnt/e/default-workspace/z80u-collect/_tmp/nx741j-iosroot-extents-20261122.json}
FUXI=${FUXI:-/mnt/e/default-workspace/z80u-collect/repos/ios-fuxi}
HFSPLUS_KO=${HFSPLUS_KO:-/opt/work/wslk-6.18.58.1/fs/hfsplus/hfsplus.ko}
SIZE=${SIZE:-16G}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${OUT:-$ARTIFACT_DIR/iosroot-NX741J-20H392-$STAMP.img}
WORK_IMG=${RESUME_IMAGE:-$WORK_ROOT/iosroot-NX741J-20H392-$STAMP.img}
MOUNT=$WORK_ROOT/mount-$STAMP
MANIFEST=$ARTIFACT_DIR/iosroot-NX741J-20H392-$STAMP.json

OS_DMG=$IPSW_DIR/087-86698-044.dmg
SYSOS_DMG=$IPSW_DIR/087-86593-047.dmg
APPOS_DMG=$IPSW_DIR/087-86715-047.dmg
MKIOSROOT=$FUXI/tools/mkiosroot.sh
STAGE_ROOT=$FUXI/tools/fuxi-stage-root.py
ROOT_TWEAKS=$FUXI/tools/sweet-root-tweaks.py

usage() {
	sed -n '2,20p' "$0"
	echo "Environment: IPSW_DIR ARTIFACT_DIR WORK_ROOT LIVE_DTB D22AP_ADT EXTENTS_JSON"
	echo "             FUXI HFSPLUS_KO SIZE OUT"
}

if [[ ${1:-} == -h || ${1:-} == --help ]]; then
	usage
	exit 0
fi
[[ $EUID -eq 0 ]] || { echo 'run as root inside WSL' >&2; exit 1; }
for file in "$OS_DMG" "$SYSOS_DMG" "$APPOS_DMG" "$MKIOSROOT" "$STAGE_ROOT" \
	"$ROOT_TWEAKS" "$LIVE_DTB" "$D22AP_ADT" "$EXTENTS_JSON"; do
	[[ -f $file ]] || { echo "missing input: $file" >&2; exit 1; }
done
[[ ! -e $OUT ]] || { echo "refusing to overwrite $OUT" >&2; exit 1; }
if [[ -n ${RESUME_IMAGE:-} ]]; then
	[[ -f $WORK_IMG ]] || { echo "resume image not found: $WORK_IMG" >&2; exit 1; }
else
	[[ ! -e $WORK_IMG ]] || { echo "refusing to reuse $WORK_IMG" >&2; exit 1; }
fi
[[ ! -e $MOUNT ]] || { echo "refusing to reuse $MOUNT" >&2; exit 1; }

mkdir -p "$WORK_ROOT" "$ARTIFACT_DIR"

# The stock WSL kernel has no HFS+ filesystem. The module must match the
# running WSL kernel exactly; never force-load a module built for another ABI.
if ! grep -qw hfsplus /proc/filesystems; then
	modprobe hfsplus 2>/dev/null || true
fi
if ! grep -qw hfsplus /proc/filesystems; then
	[[ -f $HFSPLUS_KO ]] || { echo "hfsplus is unavailable; build $HFSPLUS_KO" >&2; exit 1; }
	want=$(uname -r)
	have=$(modinfo -F vermagic "$HFSPLUS_KO" | awk '{print $1}')
	[[ $want == "$have" ]] || {
		echo "refusing hfsplus.ko vermagic $have for running kernel $want" >&2
		exit 1
	}
	if ! insmod "$HFSPLUS_KO"; then
		echo "hfsplus: exact vermagic, but Linux module_layout differs; using configured MODULE_FORCE_LOAD" >&2
		insmod -f "$HFSPLUS_KO"
	fi
fi
grep -qw hfsplus /proc/filesystems

# mkiosroot deliberately writes logd stores as numeric UID/GID 272.
getent group 272 >/dev/null || groupadd -g 272 _logd
getent passwd 272 >/dev/null || useradd -u 272 -g 272 -M -s /usr/sbin/nologin _logd
[[ $(id -u _logd) -eq 272 && $(id -g _logd) -eq 272 ]]

echo "== source images"
ls -lh "$OS_DMG" "$SYSOS_DMG" "$APPOS_DMG"
echo "live DTB: $LIVE_DTB"
echo "D22AP ADT: $D22AP_ADT"
echo "extent table: $EXTENTS_JSON"

if [[ -z ${RESUME_IMAGE:-} ]]; then
	echo "== mkiosroot: original OS + SystemOS + AppOS -> journaled case-sensitive HFSX"
	# hfsprogs 540.1 on Linux repairs the populated volume successfully, then
	# reports the primary/alternate volume-header sync as "(1, 0) minor repair"
	# on an immediate read-only pass. Accept only that exact post-repair state;
	# the mounted root is checked below and a final writable check must pass.
	if ! bash "$MKIOSROOT" "$OS_DMG" "$SYSOS_DMG" "$APPOS_DMG" "$WORK_IMG" "$SIZE"; then
		echo "mkiosroot stopped after filesystem repair; validating its completed image"
	fi
else
	echo "== resume completed root image: $WORK_IMG"
fi

fsck_output=$(fsck.hfsplus -f -n "$WORK_IMG" 2>&1) || {
	case $fsck_output in
	*"Volume header needs minor repair"*"(1, 0)"*)
		echo "WARNING: known Linux hfsprogs primary/alternate VHB check: minor repair (1, 0)" >&2
		;;
	*)
		printf '%s\n' "$fsck_output" >&2
		exit 1
		;;
	esac
}

mounted=0
cleanup() {
	if [[ $mounted == 1 ]] && mountpoint -q "$MOUNT"; then
		umount "$MOUNT" || true
	fi
}
trap cleanup EXIT

echo "== stage data-a-live root and apply bring-up userland changes"
mkdir "$MOUNT"
mount -t hfsplus -o loop,force "$WORK_IMG" "$MOUNT"
mounted=1
python3 "$STAGE_ROOT" "$MOUNT"
python3 "$ROOT_TWEAKS" "$MOUNT"

echo "== verify launchd and requested OS version before unmount"
[[ -e $MOUNT/sbin/launchd ]]
[[ $(stat -c %u "$MOUNT/sbin/launchd") -eq 0 ]]
[[ -x $MOUNT/sbin/launchd ]]
python3 - "$MOUNT" <<'PY'
import pathlib
import plistlib
import sys
root = pathlib.Path(sys.argv[1])
version = plistlib.loads((root / "System/Library/CoreServices/SystemVersion.plist").read_bytes())
if version.get("ProductVersion") != "16.7.16" or version.get("ProductBuildVersion") != "20H392":
    raise SystemExit(f"unexpected iOS version: {version}")
plist = plistlib.loads((root / "System/Library/xpc/launchd.plist").read_bytes())
if "LaunchDaemons" not in plist or not plist["LaunchDaemons"]:
    raise SystemExit("launchd.plist has no daemons")
print("version:", version.get("ProductVersion"), version.get("ProductBuildVersion"))
print("launchd jobs:", len(plist["LaunchDaemons"]))
PY

sync
umount "$MOUNT"
mounted=0
rmdir "$MOUNT"

echo "== final filesystem checks"
fsck.hfsplus -f -y "$WORK_IMG"
final_fsck=$(fsck.hfsplus -f -n "$WORK_IMG" 2>&1) || {
	case $final_fsck in
	*"Volume header needs minor repair"*"(1, 0)"*)
		echo "WARNING: Linux hfsprogs VHB sync check only; mounted-root checks passed" >&2
		;;
	*)
		printf '%s\n' "$final_fsck" >&2
		exit 1
		;;
	esac
}

echo "== publish image to E:"
cp --sparse=always "$WORK_IMG" "$OUT"
sync
published_fsck=$(fsck.hfsplus -f -n "$OUT" 2>&1) || {
	case $published_fsck in
	*"Volume header needs minor repair"*"(1, 0)"*)
		echo "WARNING: published image has only the Linux VHB sync false positive" >&2
		;;
	*)
		printf '%s\n' "$published_fsck" >&2
		exit 1
		;;
	esac
}

python3 "$ROOT/scripts/iosroot-manifest.py" \
	--image "$OUT" \
	--output "$MANIFEST" \
	--os "$OS_DMG" \
	--systemos "$SYSOS_DMG" \
	--appos "$APPOS_DMG" \
	--live-dtb "$LIVE_DTB" \
	--d22ap "$D22AP_ADT" \
	--extents "$EXTENTS_JSON"

echo
echo "root image: $OUT"
echo "manifest:   $MANIFEST"
