#!/bin/bash
set -u
source /opt/work/env.sh
R=/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
cd /opt/work/nx741j-build/UniXNU
JOBS=16 ./unixnu firmware -D $R --uefi /opt/work/uefi-sheng-main -j 16 > /mnt/e/default-workspace/z80u-collect/_tmp/fw_quiesce.log 2>&1; echo fw_rc=$?
tail -4 /mnt/e/default-workspace/z80u-collect/_tmp/fw_quiesce.log | cut -c1-200