APRIORI DXE {
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
# V15 pruned: INF QcomPkg/Drivers/IortDtDxe/IortDtDxe.inf


  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF nx741jPkg/Drivers/KernelStubDxe/KernelStubDxe.inf
  INF nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf
#  INF Binaries/nx741j/QcomPkg/Drivers/SmemDxe/SmemDxe.inf  # replaced by SmemStubDxe
# V41 disabled: INF Binaries/nx741j/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/AndroidDxe/AndroidDxe.inf
# V10 disabled (DTBExtn provider absent): INF Binaries/nx741j/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
# V14 disabled: INF Binaries/nx741j/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
# V14 disabled: INF Binaries/nx741j/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
# SKIPPED(debug):   INF Binaries/nx741j/QcomPkg/Drivers/TzDxe/TzDxeLA.inf
# V14 disabled: INF Binaries/nx741j/QcomPkg/Drivers/MailboxDxe/MailboxDxe.inf
# V14 disabled: INF Binaries/nx741j/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
# V14 disabled: INF Binaries/nx741j/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/QcomScmiDxe/QcomScmiDxe.inf
# V41 disabled: INF Binaries/nx741j/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/QupDxe/QupDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/I3CMasterDxe/I3CMasterDxe.inf
# V41 disabled: INF Binaries/nx741j/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf

  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf

# V41 disabled: INF Binaries/nx741j/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf

  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/SdccDxe/SdccDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/UFSDxe/UFSDxe.inf

  INF FatPkg/EnhancedFatDxe/Fat.inf


  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf

# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/DDRInfoDxe/DDRInfoDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/CfgUtilDxe/CfgUtilDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/EnvDxe/EnvDxeEnhanced.inf
# (dropped: OPPO-specific)
# (dropped: OPPO-specific)
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/FeatureEnablerDxe/FeatureEnablerDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/PILProxyDxe/PILProxyDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/PILDxe/PILDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/SPSSDxe/SPSSDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf

# V15 pruned: INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF EmbeddedPkg/Drivers/DtPlatformDxe/DtPlatformDxe.inf
  INF UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
# V41 disabled: INF SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  INF DarwinBootPkg/Driver/DarwinBoot.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/SPIDxe/SPIDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkSoccpDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLAGio.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/XhciPciEmulationDxe/XhciPciEmulationDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/XhciDxe/XhciDxe.inf

# V15 pruned: INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
# V15 pruned: INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
# V15 pruned: INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
# V41 disabled: INF Binaries/nx741j/QcomPkg/Drivers/ButtonsDxe/ButtonsDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
#   INF Binaries/nx741j/QcomPkg/Drivers/DisplayDxe/DisplayDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif

# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/TsensDxe/TsensDxe.inf
!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/CPRDxe/CPRDxe.inf
!endif
# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/GpiDxe/GpiDxe.inf
# V33 disabled (needs DTBExtn/HWIO/NPA): INF Binaries/nx741j/QcomPkg/Drivers/UCDxe/UCDxe.inf
# V33 disabled (needs DTBExtn/HWIO/NPA): INF Binaries/nx741j/QcomPkg/Drivers/RNGDxe/RngDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF nx741jPkg/Drivers/Nx741jKeysDxe/Nx741jKeysDxe.inf
# V27 disabled: INF nx741jPkg/Drivers/TlmmProbeDxe/TlmmProbeDxe.inf
# V23 disabled (probe removed): INF nx741jPkg/Drivers/TlmmProbeDxe/TlmmProbeDxe.inf

# V15 pruned: INF Binaries/nx741j/QcomPkg/Drivers/RscDxe/RscDxe.inf
}
