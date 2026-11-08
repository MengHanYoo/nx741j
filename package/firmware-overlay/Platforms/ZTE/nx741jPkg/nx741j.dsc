##
#
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = nx741j
  PLATFORM_GUID                  = 49301A81-23FB-4EDB-851D-1B4FBE643D04
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/nx741jPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = nx741jPkg/nx741j.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = SM8850-AC
  # 1 = SM8850-5-AC
  # 2 = SM8850-1-AD
  #
  SOC_TYPE                       = 0

!include KaanapaliPkg/KaanapaliPkg.dsc.inc

[PcdsFixedAtBuild]
  gUniXNUPkgTokenSpaceGuid.PcdUniXNUDeviceModel|"nx741j"
  # Explicit: this platform only provides a device tree, no ACPI tables.
  gEmbeddedTokenSpaceGuid.PcdDefaultDtPref|TRUE
  # V7: keep DEBUG prints but never halt on ASSERT (vendor drivers assert routinely)
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x02
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xC6E0D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"OnePlus"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"15"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"nx741j"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"CPH2747"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"24831"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1216
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2688
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  AdvancedLoggerLib|AdvLoggerPkg/Library/AdvancedLoggerLib/Dxe/AdvancedLoggerLib.inf
  AdvancedLoggerAccessLib|AdvLoggerPkg/Library/AdvancedLoggerAccessLib/AdvancedLoggerAccessLib.inf
  QrEncoderLib|MsGraphicsPkg/Library/QrEncoderLib/QrEncoderLib.inf
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  #
  # Memory Libraries
  #
  MemoryMapLib|nx741jPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|nx741jPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  Binaries/nx741j/QcomPkg/Drivers/AndroidDxe/AndroidDxe.inf
  nx741jPkg/Drivers/KernelStubDxe/KernelStubDxe.inf
  nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf
  nx741jPkg/Drivers/StopDxe/StopDxe.inf
  nx741jPkg/Drivers/TlmmProbeDxe/TlmmProbeDxe.inf
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  Binaries/nx741j/QcomPkg/Drivers/ButtonsDxe/ButtonsDxe.inf
  Binaries/nx741j/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
  Binaries/nx741j/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  Binaries/nx741j/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  Binaries/nx741j/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
#   nx741jPkg/Drivers/Nx741jKeysDxe/Nx741jKeysDxe.inf
  nx741jPkg/Drivers/SmemDumpDxe/SmemDumpDxe.inf
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
  UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf
  AdvLoggerPkg/AdvancedSerialLogger/Dxe/AdvancedSerialLoggerDxe.inf
  nx741jPkg/Drivers/LogDumpDxe/LogDumpDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
  #
  # ACPI Tables
  #
  #nx741j/AcpiTables.inf
