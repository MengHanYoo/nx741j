#!/bin/bash
# Make patch 0220: NX741J machine config = PANDORA code paths + NX741J board flag.
# NX741J (Nubia Z80 Ultra, SM8850) inherits all pandora providers (UFS/GPU/4K page),
# and only adds its own -DNX741J=1 flag for future board-specific code.
# 前提：pandora 的补丁 0102（machine config 4K）已应用。
set -euo pipefail
cd /opt/work/tree
git checkout -q -- . && git clean -qfd

# 1) SUPPORTED_ARM64_MACHINE_CONFIGS 末尾追加 NX741J（在 PANDORA 之后）
sed -i 's/^SUPPORTED_ARM64_MACHINE_CONFIGS = \(.*\) PANDORA$/SUPPORTED_ARM64_MACHINE_CONFIGS = \1 PANDORA NX741J/' makedefs/MakeInc.def

# 2) 在 MACHINE_FLAGS_ARM64_PANDORA 之后插入 NX741J 的 flags（保留 -DPANDORA=1 以继承 pandora 代码）
sed -i '/^MACHINE_FLAGS_ARM64_PANDORA = /a \
# Nubia Z80 Ultra (nx741j, SM8850): Pandora providers + NX741J board flag.\
MACHINE_FLAGS_ARM64_NX741J = -DARM64_BOARD_CONFIG_SWEET -DFUXI=1 -DPANDORA=1 -DNX741J=1 -march=armv8.2-a+crypto' makedefs/MakeInc.def

# 3) filter 里 SWEET FUXI PANDORA -> SWEET FUXI PANDORA NX741J
sed -i 's/\$(filter SWEET FUXI PANDORA,\$(CURRENT_MACHINE_CONFIG))/$(filter SWEET FUXI PANDORA NX741J,$(CURRENT_MACHINE_CONFIG))/' makedefs/MakeInc.def libkern/conf/Makefile.template bsd/conf/Makefile.template

# 4) MASTER.arm64.nx741j 从 pandora 派生（不是 fuxi）
sed 's/^# pandora (Xiaomi 17 Pro, SM8850), 4 KiB pages, otherwise as fuxi:/# nx741j (Nubia Z80 Ultra, SM8850), 4 KiB pages, otherwise as pandora:/' config/MASTER.arm64.pandora > config/MASTER.arm64.nx741j

git diff --stat
git diff > /opt/work/0220-nx741j-machine-config.patch
grep -c '' /opt/work/0220-nx741j-machine-config.patch
