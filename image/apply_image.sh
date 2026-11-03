#!/bin/bash
# Apply the built initramfs (manifest patch set) to an HFS+ iOS root image.
# Usage: apply_image.sh /path/to/image.img [/root/imgbuild.gz]
set -euo pipefail
IMG="$1"
INITRAMFS="${2:-/root/imgbuild.gz}"
[ -f "$IMG" ] || { echo "NO_IMAGE $IMG"; exit 1; }
[ -f "$INITRAMFS" ] || { echo "NO_INITRAMFS $INITRAMFS"; exit 1; }
cd /root
timeout 1800 qemu-system-x86_64 -m 8192 -cpu max \
  -kernel /boot/vmlinuz-7.0.0-38-generic -initrd "$INITRAMFS" \
  -append "console=ttyS0,115200 panic=-1 loglevel=3" \
  -drive file="$IMG",format=raw,if=virtio -nographic -no-reboot 2>&1 | tail -25