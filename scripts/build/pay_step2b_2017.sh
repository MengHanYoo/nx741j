#!/bin/bash
set -u
source /opt/work/env.sh
export PATH=/opt/work/shim:/opt/work/unixnu-linux/CommandLineTools/usr/bin:$PATH
export CROSS_EXTRA_FLAGS=''
R=/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
cd /opt/work/nx741j-build/UniXNU
rm -f out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.json out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img
./unixnu payload -D $R > /mnt/e/default-workspace/z80u-collect/_tmp/p_step2b.log 2>&1; echo payload_rc=$?
cp -f out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img /mnt/e/default-workspace/z80u-collect/_tmp/payload-step2.img 2>/dev/null
grep -a -o 'native-ufs-probe=[0-9]*' /mnt/e/default-workspace/z80u-collect/_tmp/payload-step2.img | sort -u
sha256sum /mnt/e/default-workspace/z80u-collect/_tmp/payload-step2.img 2>/dev/null || echo NO_PAYLOAD