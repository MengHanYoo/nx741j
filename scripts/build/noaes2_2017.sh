#!/bin/bash
set -u
source /opt/work/env.sh
export PATH=/opt/work/shim:/opt/work/unixnu-linux/CommandLineTools/usr/bin:$PATH
export XNU_NATIVE_AES=0
rm -rf /opt/work/work-local/ShengUfs-fd420df/*.cpp /opt/work/work-local/ShengUfs-fd420df/*.h /opt/work/work-local/ShengUfs-fd420df/Info.plist
git -C /mnt/e/default-workspace/z80u-collect/_tmp/shengufs-doc archive fd420df000f43213d9cd019725abff0849c98c72 | tar -x -C /opt/work/work-local/ShengUfs-fd420df
R=/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
O=/opt/work/nx741j-build/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j
cd /opt/work/nx741j-build/UniXNU
JOBS=8 ./unixnu kernel -D $R -j 8 > /mnt/e/default-workspace/z80u-collect/_tmp/k_noaes2.log 2>&1; echo kernel_rc=$?
ls -l $O/kernel/kernel.macho 2>/dev/null | awk '{print $5, $8}' || echo NO_MACHO
grep -a 'error' /mnt/e/default-workspace/z80u-collect/_tmp/k_noaes2.log | tail -3 | cut -c1-180