#!/bin/bash
T=/opt/work/nx741j-build/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j/tree
cp /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j/scripts/patch-native-ufs-cold-init.py $T/scripts/
cd $T || exit 1
python3 scripts/patch-native-ufs-cold-init.py || exit 1
echo -n 'smr insertions: '; grep -c 'adopt: smr=' drivers/ShengUfs/NativeUfs.cpp
bash /mnt/e/default-workspace/z80u-collect/_tmp/t2/noaes2_2017.sh > /root/kbuild.log 2>&1
echo kernel_rc=$?
tail -2 /root/kbuild.log