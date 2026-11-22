#!/bin/bash
# Build the two storage payload modes as separate artifacts.
#
#   normal-root: official iBoot/UniXNU root path
#   probe12:     read-only adopt-queue diagnostic that intentionally reports
#                publication done=1 published=2 before returning to the console
#
# The normal-root payload is left in the tree's payload/ directory after this
# script finishes; probe12 is only copied to the artifact directory.
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/wsl-env.sh"
TREE=$(resolve_unixnu_tree "${1:-}") || {
  echo "cannot find UniXNU; pass a tree path or set UNIXNU" >&2
  exit 1
}
OUT=$REPO/out/payload-modes
mkdir -p "$OUT"
cd "$TREE"

./unixnu payload -D "$REPO"
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img" \
  "$OUT/payload-normal-root.img"
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.json" \
  "$OUT/payload-normal-root.json"

./unixnu payload -D "$REPO" \
  --kernel-arg native-ufs-root=0 \
  --kernel-arg native-ufs-probe=12 \
  --kernel-arg sheng-pmgr=0 \
  --kernel-arg native-source-storage=0
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img" \
  "$OUT/payload-probe12.img"
cp -f "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.json" \
  "$OUT/payload-probe12.json"

# Restore the exact normal-root artifact instead of rebuilding it. ADT generation
# is not byte-deterministic, so a second build would make the tree, QEMU and the
# documented artifact disagree even with identical sources and kernel arguments.
cp -f "$OUT/payload-normal-root.img" "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img"
cp -f "$OUT/payload-normal-root.json" "$TREE/out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.json"
sha256sum "$OUT"/payload-*.img
