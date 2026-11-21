#!/bin/bash
# Export every tracked and untracked framework change against its pinned base.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
TREE=${1:?usage: export-framework-patch.sh UNI_XNU_TREE [OUTPUT]}
BASE=${FRAMEWORK_BASE:-ba9e14fb46cff0f2d3e8b1a006bd9f849cbd5ce6}
OUT=${2:-"$REPO/patches/framework/UniXNU-${BASE:0:8}-nx741j.patch"}

head=$(git -C "$TREE" rev-parse HEAD)
if [ "$head" != "$BASE" ]; then
  echo "framework HEAD is $head, expected pinned base $BASE" >&2
  echo "set FRAMEWORK_BASE explicitly only after re-qualifying the patch" >&2
  exit 1
fi

index=$(mktemp)
trap 'rm -f "$index"' EXIT
mkdir -p "$(dirname "$OUT")"
GIT_INDEX_FILE=$index git -C "$TREE" read-tree HEAD
GIT_INDEX_FILE=$index git -C "$TREE" add -A -- . ':(exclude)unixnu.local.toml.bak'
GIT_INDEX_FILE=$index git -C "$TREE" diff --cached --binary --full-index HEAD > "$OUT"
[ -s "$OUT" ] || {
  echo "framework working tree has no changes against $BASE" >&2
  exit 1
}
git -C "$TREE" apply --stat "$OUT" >&2
echo "framework patch: $OUT"
