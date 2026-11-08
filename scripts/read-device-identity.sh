#!/usr/bin/env bash
# Print the device identity used by build.unique_chip_id and the payload platform seed.
# The value comes from the bootloader fuse: ro.boot.unique_chip_id.
set -eu
ADB=${ADB:-adb}
SERIAL=$($ADB shell getprop ro.boot.unique_chip_id | tr -d '\r')
if [ -z "$SERIAL" ]; then
  echo "ro.boot.unique_chip_id is empty; check USB debugging and that the device booted Android" >&2
  exit 1
fi
echo "ro.boot.unique_chip_id=$SERIAL"
echo "Update unixnu.local.toml: unique_chip_id = \"$SERIAL\""
