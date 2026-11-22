#!/bin/bash
# Install only the 4 KiB NXROOTP1 locator into the final userdata block.
# The anchor verifies the existing SHNGROOT image; this script never moves or
# rewrites the root image. Dry-run is the default.
set -eu

ANCHOR=
EXECUTE=0
BLOCK=/dev/block/by-name/userdata
ADB=${ADB:-adb}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --anchor) ANCHOR=${2:?--anchor needs a path}; shift 2 ;;
    --block) BLOCK=${2:?--block needs a path}; shift 2 ;;
    --adb) ADB=${2:?--adb needs a path}; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    *) echo "usage: install-userdata-anchor.sh --anchor FILE [--block DEV] [--adb ADB] [--execute]" >&2; exit 2 ;;
  esac
done
[ -n "$ANCHOR" ] && [ -f "$ANCHOR" ] || {
  echo 'install-userdata-anchor: --anchor must name an existing file' >&2
  exit 2
}
"$ADB" wait-for-device
"$ADB" root >/dev/null
"$ADB" wait-for-device

size=$("$ADB" shell blockdev --getsize64 "$BLOCK" | tr -d '\r')
case "$size" in ''|*[!0-9]*) echo 'install-userdata-anchor: cannot read partition size' >&2; exit 1 ;; esac
last=$((size / 4096 - 1))
[ "$last" -ge 0 ] || { echo 'install-userdata-anchor: partition is smaller than one block' >&2; exit 1; }

eval "$(python3 - "$ANCHOR" "$size" <<'PY'
import sys
from pathlib import Path

anchor = Path(sys.argv[1]).read_bytes()
size = int(sys.argv[2])
if len(anchor) != 4096 or anchor[:8] != b"NXROOTP1":
    raise SystemExit("invalid anchor")
manifest = int.from_bytes(anchor[32:40], "little")
expected = anchor[56:88].hex()
print(f"MANIFEST_LBA={manifest}")
print(f"EXPECTED_SHA={expected}")
print(f"DEVICE_BYTES={int.from_bytes(anchor[16:24], 'little')}")
print(f"PARTITION_LAST={int.from_bytes(anchor[24:32], 'little')}")
print(f"LAST_BLOCK={size // 4096 - 1}")
PY
)"
[ "$LAST_BLOCK" -eq "$last" ] && [ "$PARTITION_LAST" -eq "$last" ] || {
  echo "install-userdata-anchor: anchor partition end $PARTITION_LAST != device $last" >&2
  exit 1
}

actual=$("$ADB" exec-out dd if="$BLOCK" bs=4096 skip="$MANIFEST_LBA" count=1 2>/dev/null | sha256sum | awk '{print $1}')
[ "$actual" = "$EXPECTED_SHA" ] || {
  echo "install-userdata-anchor: manifest at LBA $MANIFEST_LBA does not match the anchor" >&2
  echo "expected $EXPECTED_SHA" >&2
  echo "actual   $actual" >&2
  exit 1
}

echo "device=$BLOCK bytes=$size last_block=$last"
echo "manifest_lba=$MANIFEST_LBA sha256=$EXPECTED_SHA"
echo "anchor=$ANCHOR"
if [ "$EXECUTE" -ne 1 ]; then
  echo 'dry run: pass --execute to write the verified 4 KiB anchor block'
  exit 0
fi

remote=/data/local/tmp/nx741j-root-anchor.bin
"$ADB" push "$ANCHOR" "$remote" >/dev/null
"$ADB" shell dd if="$remote" of="$BLOCK" bs=4096 seek="$LAST_BLOCK" count=1 conv=fsync,notrunc
"$ADB" shell rm -f "$remote"
readback=$("$ADB" exec-out dd if="$BLOCK" bs=4096 skip="$LAST_BLOCK" count=1 2>/dev/null | sha256sum | awk '{print $1}')
original=$(sha256sum "$ANCHOR" | awk '{print $1}')
[ "$readback" = "$original" ] || {
  echo 'install-userdata-anchor: readback differs from the host anchor' >&2
  exit 1
}
echo "PASS  anchor installed and read back at absolute userdata block $LAST_BLOCK"
