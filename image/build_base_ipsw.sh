#!/bin/bash
# build_base_ipsw.sh: rebuild the iOS root HFS+ image from the complete IPSW DMGs.
# WSL has no hfsplus, so the tree is staged on ext4 and extracted into the HFS+
# image inside QEMU (hfsplus.ko from the port initramfs template).
set -euo pipefail
W=/root/iosbuild
IPSW=/mnt/e/default-workspace/ipsw
OUT=$W/out.img
log() { echo "[$(date +%H:%M:%S)] $*"; }
log "work=$W"
rm -rf $W && mkdir -p $W
mkdir -p $W/os $W/sysos $W/appos
log "mounting DMGs"
apfs-fuse $IPSW/087-86698-044.dmg $W/os
apfs-fuse $IPSW/087-86593-047.dmg $W/sysos
apfs-fuse $IPSW/087-86715-047.dmg $W/appos
ls $W/os/root >/dev/null && log "os mounted"
log "staging tree (rsync)"
mkdir -p $W/stage
rsync -aH --numeric-ids $W/os/root/ $W/stage/
mkdir -p $W/stage/private/preboot/Cryptexes/OS $W/stage/private/preboot/Cryptexes/App
rsync -aH --numeric-ids $W/sysos/root/ $W/stage/private/preboot/Cryptexes/OS/
rsync -aH --numeric-ids $W/appos/root/ $W/stage/private/preboot/Cryptexes/App/
log "stage size: $(du -sh $W/stage | cut -f1)"
log "hard linking cryptex files"
dsc=private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld
mkdir -p $W/stage/System/Library/Caches/com.apple.dyld
for f in $W/stage/$dsc/*; do ln "$f" "$W/stage/System/Library/Caches/com.apple.dyld/${f##*/}"; done
for c in OS App; do
  (cd $W/stage/private/preboot/Cryptexes/$c && find . ! -type d ! -path "./System/Library/Caches/com.apple.dyld/*") | while IFS= read -r f; do
    f=${f#./}
    if [ ! -e "$W/stage/$f" ] && [ ! -L "$W/stage/$f" ]; then
      mkdir -p "$W/stage/$(dirname "$f")"
      src="$W/stage/private/preboot/Cryptexes/$c/$f"
      if [ -L "$src" ]; then cp -P "$src" "$W/stage/$f"; else ln "$src" "$W/stage/$f"; fi
    fi
  done
done
log "logd dirs + keybag"
db=$W/stage/private/var/db
install -d -o 272 -g 272 -m 0751 "$db/diagnostics"
install -d -o 272 -g 272 -m 0755 "$db/uuidtext" "$db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do install -o 272 -g 272 -m 0644 /dev/null "$db/$d/.migrated"; done
install -d -m 0700 -o 0 -g 0 "$W/stage/private/var/keybags"
printf "%s\n" '<?xml version="1.0" encoding="UTF-8"?>' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' '<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' > $W/stage/private/var/keybags/systembag.kb
chmod 0600 $W/stage/private/var/keybags/systembag.kb
log "packing tar"
tar -C $W/stage -cf $W/payload.tar .
log "payload.tar $(du -h $W/payload.tar | cut -f1)"
log "data image"
truncate -s 24G $W/data.img
mkfs.ext4 -q -F $W/data.img
mkdir -p $W/data
mount -o loop $W/data.img $W/data
cp $W/payload.tar $W/data/
sync; umount $W/data
log "hfs image"
truncate -s 16G $OUT
mkfs.hfsplus -s -J -v iOSRoot $OUT
log "QEMU extract"
Q=/root/iosbuild/initramfs.gz
rm -rf $W/init && mkdir -p $W/init/files
cp -r /root/hfswork/init/bin $W/init/
cp /root/hfswork/init/hfsplus.ko /root/hfswork/init/nls_utf8.ko /root/hfswork/init/nls_iso8859-1.ko $W/init/
cat > $W/init/init <<'EOS'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mkdir -p /proc /sys /dev /mnt /data
mount -t proc proc /proc
mount -t devtmpfs dev /dev
insmod /nls_utf8.ko 2>/dev/null
insmod /hfsplus.ko && echo INSMOD_OK
mount -t hfsplus -o force,nls=utf8 /dev/vda /mnt && echo MOUNT_HFS_OK || { echo MOUNT_HFS_FAIL; exec /bin/sh; }
mount -t ext4 /dev/vdb /data && echo MOUNT_EXT4_OK || { echo MOUNT_EXT4_FAIL; exec /bin/sh; }
echo "extracting..."
tar xpf /data/payload.tar -C /mnt && echo TAR_OK || echo TAR_FAIL
sync
umount /mnt && echo UMOUNT_OK
umount /data
poweroff -f
EOS
chmod +x $W/init/init
(cd $W/init && find . | cpio -o -H newc 2>/dev/null | gzip -1 > $Q)
log "initramfs $(du -h $Q | cut -f1)"
timeout 3600 qemu-system-x86_64 -m 4096 -cpu max -kernel /boot/vmlinuz-7.0.0-38-generic -initrd $Q -append "console=ttyS0,115200 panic=-1 loglevel=3" -drive file=$OUT,format=raw,if=virtio -drive file=$W/data.img,format=raw,if=virtio -nographic -no-reboot 2>&1 | tail -20
log "fsck"
fsck.hfsplus -f -y $OUT | tail -3 || true
fsck.hfsplus -f -n $OUT | tail -2
log "copy to E:"
cp $OUT /mnt/e/default-workspace/z80u-collect/iosroot-IPSW.img
ls -la /mnt/e/default-workspace/z80u-collect/iosroot-IPSW.img
echo BUILD_BASE_DONE