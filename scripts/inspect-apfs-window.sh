#!/bin/bash
# List APFS volumes inside one SHNGROOT userdata window without mounting it.
set -eu

IMAGE=${1:?usage: inspect-apfs-window.sh IMAGE WINDOW_OFFSET WINDOW_SIZE [CONTAINER_BASE]}
WINDOW_OFFSET=${2:?}
WINDOW_SIZE=${3:?}
CONTAINER_BASE=${4:-0x4400}

command -v losetup >/dev/null || {
  echo 'losetup is required' >&2
  exit 1
}
command -v apfsutil >/dev/null || {
  echo 'apfsutil is required; build/install apfs-fuse first' >&2
  exit 1
}

offset=$((WINDOW_OFFSET + CONTAINER_BASE))
size=$WINDOW_SIZE
if (( CONTAINER_BASE > 0 )); then
  size=$((WINDOW_SIZE - CONTAINER_BASE))
fi

loop=$(losetup -r -f --show --offset "$offset" --sizelimit "$size" "$IMAGE")
trap 'losetup -d "$loop"' EXIT
echo "window=$WINDOW_OFFSET size=$WINDOW_SIZE container=$offset loop=$loop"
apfsutil "$loop"
