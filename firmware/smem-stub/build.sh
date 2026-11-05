#!/bin/bash
# Incremental EDK2 build of the nx741j firmware with the SMEM stub driver.
set -eu
T=/opt/work/uefi-sheng-main
OUT=/mnt/e/default-workspace/z80u-collect/repos/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j

export WORKSPACE="$T"
export PACKAGES_PATH="$T:$T/Mu_Basecore:$T/Common/Mu:$T/Common/Mu_OEM_Sample:$T/Silicon/Qualcomm:$T/Silicon/Silicium:$T/Platforms/ZTE:$OUT/tree/package:$OUT/tree/drivers/DarwinBoot:/opt/work/refsrc/edk2"
export EDK_TOOLS_PATH=/opt/work/refsrc/edk2/BaseTools
export CONF_PATH="$OUT/edk2-conf"
export PYTHON_COMMAND=python3
export PATH="$EDK_TOOLS_PATH/Source/C/bin:$EDK_TOOLS_PATH/BinWrappers/PosixLike:$OUT/toolchain:$PATH"
export PYTHONPATH="$EDK_TOOLS_PATH/Source/Python"

cd "$T"
python3 "$EDK_TOOLS_PATH/Source/Python/build/build.py" \
  -a AARCH64 -t CLANGPDB -b DEBUG \
  -p "$T/Platforms/ZTE/nx741jPkg/nx741j.dsc" \
  -D FD_BASE=0xC6800000 -D FD_SIZE=0x400000 -D FD_BLOCKS=0x400 \
  -D ENABLE_SECUREBOOT=0 -D DEVICE_MODEL=0 \
  -n 8 \
  "$@"
