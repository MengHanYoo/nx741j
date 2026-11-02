#!/bin/bash
set -euo pipefail
W=/root/seedtar
rm -rf $W && mkdir -p $W/init/files
truncate -s 6G $W/data.img
mkfs.ext4 -q -F $W/data.img
cp -r /root/hfswork/init/bin $W/init/
cp /root/hfswork/init/hfsplus.ko /root/hfswork/init/nls_utf8.ko $W/init/
cp /mnt/e/default-workspace/z80u-collect/wsl/seed_tar_init $W/init/init
chmod +x $W/init/init
(cd $W/init && find . | cpio -o -H newc 2>/dev/null | gzip -1 > $W/initramfs.gz)
echo "initramfs $(du -h $W/initramfs.gz | cut -f1)"
timeout 1800 qemu-system-x86_64 -m 4096 -cpu max -kernel /boot/vmlinuz-7.0.0-38-generic -initrd $W/initramfs.gz -append "console=ttyS0,115200 panic=-1 loglevel=3" -drive file=/mnt/e/default-workspace/z80u-collect/iosroot-GOLDEN.img,format=raw,if=virtio,readonly=on -drive file=$W/data.img,format=raw,if=virtio -nographic -no-reboot 2>&1 | tail -12