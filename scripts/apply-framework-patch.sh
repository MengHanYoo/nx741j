#!/bin/bash
# Apply the repository's complete UniXNU port patch to a clean pinned checkout.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
BASE=ba9e14fb46cff0f2d3e8b1a006bd9f849cbd5ce6
PATCH=$REPO/patches/framework/UniXNU-${BASE:0:8}-nx741j.patch
TREE=${1:?usage: apply-framework-patch.sh UNI_XNU_TREE}

[ "$(git -C "$TREE" rev-parse --is-inside-work-tree 2>/dev/null || true)" = true ] || {
  echo "not a Git checkout: $TREE" >&2
  exit 1
}
[ -f "$PATCH" ] || {
  echo "framework patch missing: $PATCH" >&2
  exit 1
}

head=$(git -C "$TREE" rev-parse HEAD)
if [ "$head" != "$BASE" ]; then
  echo "framework HEAD is $head, expected $BASE" >&2
  exit 1
fi

if git -C "$TREE" apply --check "$PATCH" >/dev/null 2>&1; then
  git -C "$TREE" apply "$PATCH"
  echo "applied $PATCH"
elif git -C "$TREE" apply --reverse --check "$PATCH" >/dev/null 2>&1; then
  echo "framework patch is already applied"
else
  echo "framework tree is neither clean at $BASE nor an exact patched result" >&2
  exit 1
fi
