#!/bin/bash
# 把 NativeAdrenoMetal.bundle 装进 iOS 根镜像的 /System/Library/Extensions/
set -u
IMG=/mnt/c/Users/MengHanYooo/Documents/deepseek-harness/default-workspace/z80u-collect/iospull.img
W=/root/hfswork
SRC=/opt/work/gpu/metal-out/NativeAdrenoMetal.bundle

echo '=== 把 bundle 放进 initramfs ==='
rm -rf $W/init/NativeAdrenoMetal.bundle
cp -r "$SRC" $W/init/NativeAdrenoMetal.bundle
du -sh $W/init/NativeAdrenoMetal.bundle

echo '=== 修卷 ==='
fsck.hfsplus -fy "$IMG" 2>&1 | tail -2

cat > $W/init/init <<'EOF'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mkdir -p /proc /sys /dev
mount -t proc proc /proc 2>/dev/null
mount -t devtmpfs dev /dev 2>/dev/null
insmod /nls_base.ko 2>/dev/null; insmod /nls_utf8.ko 2>/dev/null
insmod /hfsplus.ko && echo INSMOD_OK
mkdir -p /mnt
mount -t hfsplus -o force,nls=utf8 /dev/vda /mnt && echo MOUNT_OK || { echo MOUNT_FAIL; exec /bin/sh; }
EXT=/mnt/System/Library/Extensions
echo '=== Extensions 目录 ==='
ls -d "$EXT" 2>&1 | head -2
mkdir -p "$EXT/NativeAdrenoMetal.bundle"
cp /NativeAdrenoMetal.bundle/NativeAdrenoMetal "$EXT/NativeAdrenoMetal.bundle/NativeAdrenoMetal"
cp /NativeAdrenoMetal.bundle/Info.plist "$EXT/NativeAdrenoMetal.bundle/Info.plist"
chmod 755 "$EXT/NativeAdrenoMetal.bundle" "$EXT/NativeAdrenoMetal.bundle/NativeAdrenoMetal"
chmod 644 "$EXT/NativeAdrenoMetal.bundle/Info.plist"
chown -R 0:0 "$EXT/NativeAdrenoMetal.bundle"
echo '=== 安装结果 ==='
ls -lan "$EXT/NativeAdrenoMetal.bundle"
wc -c < "$EXT/NativeAdrenoMetal.bundle/NativeAdrenoMetal"
sync
umount /mnt && echo UMOUNT_OK
echo ALL_DONE
poweroff -f
EOF
chmod +x $W/init/init
cd $W/init && find . | cpio -o -H newc 2>/dev/null | gzip -9 > $W/hfsinit.gz
ls -la $W/hfsinit.gz

echo '=== QEMU 写入 ==='
cd /root
timeout 2400 qemu-system-x86_64 -m 2048 -cpu max -kernel /boot/vmlinuz-7.0.0-38-generic \
  -initrd $W/hfsinit.gz -append 'console=ttyS0,115200 panic=-1 loglevel=3' \
  -drive file=$IMG,format=raw,if=virtio -nographic -no-reboot 2>&1 | tail -20
