/**
  DO NOT EDIT
  FILE auto-generated
  Module name:
    AutoGen.h
  Abstract:       Auto-generated AutoGen.h for building module or library.
**/

#ifndef _AUTOGENH_E11737A2_C846_41C1_850A_594DB97C34C9
#define _AUTOGENH_E11737A2_C846_41C1_850A_594DB97C34C9

#ifdef __cplusplus
extern "C" {
#endif

#include <Uefi.h>

extern GUID  gEfiCallerIdGuid;
extern GUID  gEdkiiDscPlatformGuid;
extern CHAR8 *gEfiCallerBaseName;

#define EFI_CALLER_ID_GUID \
  {0xE11737A2, 0xC846, 0x41C1, {0x85, 0x0A, 0x59, 0x4D, 0xB9, 0x7C, 0x34, 0xC9}}
#define EDKII_DSC_PLATFORM_GUID \
  {0x1C1F7C2E, 0x7A84, 0x4F7B, {0x9C, 0x55, 0x2B, 0x7E, 0x4C, 0x0F, 0x5A, 0x31}}
#define STACK_COOKIE_VALUE 0x268148032DCD3F9EULL

// Guids
extern EFI_GUID gEfiMdePkgTokenSpaceGuid;

// Protocols
extern EFI_GUID gUniXNUSampleProtocolGuid;
extern EFI_GUID gEfiLoadedImageProtocolGuid;

// Definition of SkuId Array
extern UINT64 _gPcd_SkuId_Array[];

// Definition of PCDs used in libraries is in AutoGen.c


EFI_STATUS
EFIAPI
SampleDriverEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );





#ifdef __cplusplus
}
#endif

#endif
