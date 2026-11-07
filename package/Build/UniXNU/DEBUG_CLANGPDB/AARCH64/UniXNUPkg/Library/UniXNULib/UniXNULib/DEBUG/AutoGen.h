/**
  DO NOT EDIT
  FILE auto-generated
  Module name:
    AutoGen.h
  Abstract:       Auto-generated AutoGen.h for building module or library.
**/

#ifndef _AUTOGENH_2EA3E316_7EC4_4867_9FEA_ABE1D4E9DBA6
#define _AUTOGENH_2EA3E316_7EC4_4867_9FEA_ABE1D4E9DBA6

#ifdef __cplusplus
extern "C" {
#endif

#include <Uefi.h>
#include <Library/PcdLib.h>

extern GUID  gEfiCallerIdGuid;
extern GUID  gEdkiiDscPlatformGuid;
extern CHAR8 *gEfiCallerBaseName;

#define STACK_COOKIE_VALUE 0x62A9367CBC2281AEULL

// Guids
extern EFI_GUID gEfiFileInfoGuid;
extern EFI_GUID gFdtTableGuid;
extern EFI_GUID gLinuxEfiInitrdMediaGuid;
extern EFI_GUID gUniXNUBlockSliceGuid;
extern EFI_GUID gUniXNUPkgTokenSpaceGuid;

// Protocols
extern EFI_GUID gEfiAcpiTableProtocolGuid;
extern EFI_GUID gEfiBlockIoProtocolGuid;
extern EFI_GUID gEfiPartitionInfoProtocolGuid;
extern EFI_GUID gEfiDevicePathProtocolGuid;
extern EFI_GUID gEfiFirmwareVolume2ProtocolGuid;
extern EFI_GUID gEfiGraphicsOutputProtocolGuid;
extern EFI_GUID gEfiLoadedImageProtocolGuid;
extern EFI_GUID gEfiLoadFile2ProtocolGuid;
extern EFI_GUID gEfiSerialIoProtocolGuid;
extern EFI_GUID gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID gEfiSimpleTextInProtocolGuid;
extern EFI_GUID gUniXNULoaderProtocolGuid;
extern EFI_GUID gUniXNUMenuPluginProtocolGuid;

// Definition of SkuId Array
extern UINT64 _gPcd_SkuId_Array[];

// PCD definitions
#define _PCD_TOKEN_PcdUniXNUDebugLevel  0U
extern const UINT32 _gPcd_FixedAtBuild_PcdUniXNUDebugLevel;
#define _PCD_GET_MODE_32_PcdUniXNUDebugLevel  _gPcd_FixedAtBuild_PcdUniXNUDebugLevel
//#define _PCD_SET_MODE_32_PcdUniXNUDebugLevel  ASSERT(FALSE)  // It is not allowed to set value for a FIXED_AT_BUILD PCD
#define _PCD_VALUE_PcdUniXNUDebugLevel 0x00000040
#define _PCD_SIZE_PcdUniXNUDebugLevel 4
#define _PCD_GET_MODE_SIZE_PcdUniXNUDebugLevel _PCD_SIZE_PcdUniXNUDebugLevel
#define _PCD_TOKEN_PcdUniXNUConfigPath  0U
extern const UINT16 _gPcd_FixedAtBuild_PcdUniXNUConfigPath[];
#define _PCD_GET_MODE_PTR_PcdUniXNUConfigPath  _gPcd_FixedAtBuild_PcdUniXNUConfigPath
//#define _PCD_SET_MODE_PTR_PcdUniXNUConfigPath  ASSERT(FALSE)  // It is not allowed to set value for a FIXED_AT_BUILD PCD
#define _PCD_VALUE_PcdUniXNUConfigPath _gPcd_FixedAtBuild_PcdUniXNUConfigPath
#define _PCD_SIZE_PcdUniXNUConfigPath 54
#define _PCD_GET_MODE_SIZE_PcdUniXNUConfigPath _PCD_SIZE_PcdUniXNUConfigPath


#ifdef __cplusplus
}
#endif

#endif
