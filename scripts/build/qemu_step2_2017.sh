#!/bin/bash
cd /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
QEMU_MEM=8G QEMU_SMP=4 bash scripts/qemu-nx741j.sh /opt/work/nx741j-build/UniXNU 30 > /mnt/e/default-workspace/z80u-collect/_tmp/qemu_step2.log 2>&1; echo qemu_rc=$?
grep -E 'UNIXNU: BootEntry|MARK #29|EBS ok' /mnt/e/default-workspace/z80u-collect/_tmp/qemu_step2.log | tail -3