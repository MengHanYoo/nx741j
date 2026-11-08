#!/usr/bin/env bash
# Print the device identity used by build.unique_chip_id and the payload platform seed.
# The value comes from the bootloader fuse: ro.boot.unique_chip_id.
set -eu
ADB=${ADB:-adb}
SERIAL=$($ADB shell getprop ro.boot.unique_chip_id | tr -d '\r')
if [ -z "$SERIAL" ]; then
  # This ROM leaves ro.boot.unique_chip_id empty; the QFPROM-backed SoC serial is the next source.
  SERIAL=$($ADB shell cat /sys/devices/soc0/serial_number | tr -d '\r')
  if [ -z "$SERIAL" ]; then
    echo "no unique_chip_id and no soc serial; check USB debugging and that the device booted Android" >&2
    exit 1
  fi
  printf 'soc_serial_decimal=%s\n' "$SERIAL"
  SERIAL=$(printf '%016x' "$SERIAL")
fi
echo "unique_chip_id=$SERIAL"
echo "Update unixnu.local.toml: unique_chip_id = \"$SERIAL\""
