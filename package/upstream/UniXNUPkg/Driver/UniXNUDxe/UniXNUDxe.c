/** @file
  UniXNUDxe: registers the UniXNU application in this firmware volume
  as the first boot option.

  Only standard UEFI variables are used (Boot####, BootOrder). On platforms
  whose variable store is RAM-backed the option is simply re-created on every
  boot.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>

#include <Guid/GlobalVariable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadedImage.h>

#define OPTION_ATTRIBUTES  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

//
// Optional data of the boot option; the application treats it as "started
// by firmware". Keep in sync with UniXNU.c.
//
STATIC CONST CHAR16  mFirmwareMarker[] = L"unixnu:firmware";
STATIC CONST CHAR16  mDescription[]    = L"UniXNU";

STATIC
EFI_STATUS
StartApplicationFromOwnFv (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL        *Loaded;
  EFI_FIRMWARE_VOLUME2_PROTOCOL    *Fv;
  CONST EFI_GUID                   *File;
  VOID                             *Source;
  UINTN                            SourceSize;
  EFI_FV_FILETYPE                  Type;
  EFI_FV_FILE_ATTRIBUTES           Attributes;
  UINT32                           AuthStatus;
  EFI_HANDLE                       AppImage;
  struct {
    MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  File;
    EFI_DEVICE_PATH_PROTOCOL           End;
  } AppPath;

  Loaded = NULL;
  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&Loaded
                  );
  if (EFI_ERROR (Status) || (Loaded == NULL) || (Loaded->DeviceHandle == NULL)) {
    Print (L"UNIXNU-DXE FV: LoadedImage=%r device=%p\n", Status, Loaded ? Loaded->DeviceHandle : NULL);
    return EFI_NOT_FOUND;
  }

  Fv = NULL;
  Status = gBS->HandleProtocol (
                  Loaded->DeviceHandle,
                  &gEfiFirmwareVolume2ProtocolGuid,
                  (VOID **)&Fv
                  );
  if (EFI_ERROR (Status) || (Fv == NULL)) {
    Print (L"UNIXNU-DXE FV: FirmwareVolume2=%r\n", Status);
    return EFI_NOT_FOUND;
  }

  File = (CONST EFI_GUID *)PcdGetPtr (PcdUniXNUApplicationFile);
  Source     = NULL;
  SourceSize = 0;
  Status = Fv->ReadFile (
                 Fv,
                 File,
                 &Source,
                 &SourceSize,
                 &Type,
                 &Attributes,
                 &AuthStatus
                 );
  if (EFI_ERROR (Status) || (Source == NULL) || (SourceSize == 0)) {
    Print (L"UNIXNU-DXE FV: ReadFile=%r size=%Lu\n", Status, (UINT64)SourceSize);
    return EFI_NOT_FOUND;
  }

  ZeroMem (&AppPath, sizeof (AppPath));
  EfiInitializeFwVolDevicepathNode (&AppPath.File, File);
  AppPath.End.Type      = END_DEVICE_PATH_TYPE;
  AppPath.End.SubType   = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  AppPath.End.Length[0] = sizeof (EFI_DEVICE_PATH_PROTOCOL);
  AppPath.End.Length[1] = 0;

  AppImage = NULL;
  Status = gBS->LoadImage (
                  FALSE,
                  ImageHandle,
                  (EFI_DEVICE_PATH_PROTOCOL *)&AppPath,
                  Source,
                  SourceSize,
                  &AppImage
                  );
  FreePool (Source);
  if (EFI_ERROR (Status)) {
    Print (L"UNIXNU-DXE FV: source LoadImage=%r\n", Status);
    DEBUG ((DEBUG_ERROR, "UniXNUDxe: own-FV LoadImage: %r\n", Status));
    return Status;
  }

  Print (L"UNIXNU-DXE FV: StartImage\n");
  DEBUG ((DEBUG_ERROR, "UniXNUDxe: starting UniXNU from own firmware volume\n"));
  return gBS->StartImage (AppImage, NULL, NULL);
}

STATIC
EFI_STATUS
StartApplicationFromOwnPath (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;
  EFI_DEVICE_PATH_PROTOCOL   *Path;
  EFI_DEVICE_PATH_PROTOCOL   *Node;
  CONST EFI_GUID             *File;
  BOOLEAN                    Replaced;
  EFI_HANDLE                 AppImage;

  Loaded = NULL;
  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&Loaded
                  );
  if (EFI_ERROR (Status) || (Loaded == NULL) || (Loaded->FilePath == NULL)) {
    Print (L"UNIXNU-DXE PATH: LoadedImage=%r\n", Status);
    return EFI_NOT_FOUND;
  }

  Path = DuplicateDevicePath (Loaded->FilePath);
  if (Path == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  File     = (CONST EFI_GUID *)PcdGetPtr (PcdUniXNUApplicationFile);
  Replaced = FALSE;
  for (Node = Path; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
        (DevicePathSubType (Node) == MEDIA_PIWG_FW_FILE_DP)) {
      CopyMem (
        &((MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)Node)->FvFileName,
        File,
        sizeof (*File)
        );
      Replaced = TRUE;
      break;
    }
  }

  if (!Replaced) {
    FreePool (Path);
    Print (L"UNIXNU-DXE PATH: no firmware file node\n");
    return EFI_NOT_FOUND;
  }

  AppImage = NULL;
  Status = gBS->LoadImage (
                  TRUE,
                  ImageHandle,
                  Path,
                  (VOID *)mFirmwareMarker,
                  sizeof (mFirmwareMarker),
                  &AppImage
                  );
  FreePool (Path);
  if (EFI_ERROR (Status)) {
    Print (L"UNIXNU-DXE PATH: LoadImage=%r\n", Status);
    return Status;
  }

  Print (L"UNIXNU-DXE PATH: StartImage\n");
  return gBS->StartImage (AppImage, NULL, NULL);
}

STATIC
BOOLEAN
FvHasFile (
  IN EFI_HANDLE      Handle,
  IN CONST EFI_GUID  *File
  )
{
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv;
  EFI_FV_FILETYPE                Type;
  EFI_FV_FILE_ATTRIBUTES         Attributes;
  UINT32                         AuthStatus;
  UINTN                          Size;

  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiFirmwareVolume2ProtocolGuid, (VOID **)&Fv))) {
    return FALSE;
  }

  return !EFI_ERROR (Fv->ReadFile (Fv, File, NULL, &Size, &Type, &Attributes, &AuthStatus));
}

//
// Device path of the application file: our own FV first, then any FV.
//
STATIC
EFI_DEVICE_PATH_PROTOCOL *
FindApplication (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_LOADED_IMAGE_PROTOCOL          *Loaded;
  CONST EFI_GUID                     *File;
  EFI_HANDLE                         Fv;
  EFI_HANDLE                         *Handles;
  UINTN                              Count;
  UINTN                              Index;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  Node;

  File = (CONST EFI_GUID *)PcdGetPtr (PcdUniXNUApplicationFile);
  Fv   = NULL;
  if (!EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded)) &&
      FvHasFile (Loaded->DeviceHandle, File))
  {
    Fv = Loaded->DeviceHandle;
  }

  if ((Fv == NULL) &&
      !EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiFirmwareVolume2ProtocolGuid, NULL, &Count, &Handles)))
  {
    for (Index = 0; Index < Count && Fv == NULL; Index++) {
      if (FvHasFile (Handles[Index], File)) {
        Fv = Handles[Index];
      }
    }

    FreePool (Handles);
  }

  if ((Fv == NULL) || (DevicePathFromHandle (Fv) == NULL)) {
    return NULL;
  }

  EfiInitializeFwVolDevicepathNode (&Node, File);
  return AppendDevicePathNode (DevicePathFromHandle (Fv), (EFI_DEVICE_PATH_PROTOCOL *)&Node);
}

//
// Serialize an EFI_LOAD_OPTION (UEFI spec 3.1.3).
//
STATIC
VOID *
BuildLoadOption (
  IN  EFI_DEVICE_PATH_PROTOCOL  *Path,
  OUT UINTN                     *Size
  )
{
  UINTN  PathSize;
  UINT8  *Option;
  UINT8  *P;

  PathSize = GetDevicePathSize (Path);
  *Size    = sizeof (UINT32) + sizeof (UINT16) + sizeof (mDescription) + PathSize + sizeof (mFirmwareMarker);
  Option   = AllocateZeroPool (*Size);
  if (Option == NULL) {
    return NULL;
  }

  P = Option;
  WriteUnaligned32 ((UINT32 *)P, LOAD_OPTION_ACTIVE);
  P += sizeof (UINT32);
  WriteUnaligned16 ((UINT16 *)P, (UINT16)PathSize);
  P += sizeof (UINT16);
  CopyMem (P, mDescription, sizeof (mDescription));
  P += sizeof (mDescription);
  CopyMem (P, Path, PathSize);
  P += PathSize;
  CopyMem (P, mFirmwareMarker, sizeof (mFirmwareMarker));
  return Option;
}

EFI_STATUS
EFIAPI
UniXNUDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  VOID                      *Option;
  UINTN                     OptionSize;
  UINT16                    *Order;
  UINTN                     OrderSize;
  UINT16                    *NewOrder;
  UINTN                     Index;
  UINTN                     Out;
  UINT16                    Number;
  CHAR16                    Name[sizeof ("Boot####")];
  VOID                      *Existing;
  UINTN                     ExistingSize;
  BOOLEAN                   Found;
  EFI_HANDLE                AppImage;
  EFI_STATUS                AppStatus;

  Print (L"UNIXNU-DXE: entry\n");
  Status = StartApplicationFromOwnFv (ImageHandle);
  if (!EFI_ERROR (Status)) {
    return Status;
  }
  Print (L"UNIXNU-DXE: own-FV skipped %r\n", Status);

  Status = StartApplicationFromOwnPath (ImageHandle);
  if (!EFI_ERROR (Status)) {
    return Status;
  }
  Print (L"UNIXNU-DXE: own-path skipped %r\n", Status);

  Path = FindApplication (ImageHandle);
  if (Path == NULL) {
    DEBUG ((DEBUG_ERROR, "UniXNUDxe: application %g not found in any FV\n", PcdGetPtr (PcdUniXNUApplicationFile)));
    return EFI_NOT_FOUND;
  }

  Option = BuildLoadOption (Path, &OptionSize);
  if (Option == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Order     = NULL;
  OrderSize = 0;
  GetEfiGlobalVariable2 (EFI_BOOT_ORDER_VARIABLE_NAME, (VOID **)&Order, &OrderSize);

  //
  // Reuse an identical Boot#### if one exists, otherwise take the lowest
  // free number.
  //
  Found = FALSE;
  for (Index = 0; Index < OrderSize / sizeof (UINT16) && !Found; Index++) {
    UnicodeSPrint (Name, sizeof (Name), L"Boot%04x", Order[Index]);
    if (!EFI_ERROR (GetEfiGlobalVariable2 (Name, &Existing, &ExistingSize))) {
      Found = (ExistingSize == OptionSize) && (CompareMem (Existing, Option, OptionSize) == 0);
      if (Found) {
        Number = Order[Index];
      }

      FreePool (Existing);
    }
  }

  if (!Found) {
    for (Number = 0; Number < 0xFFFF; Number++) {
      UnicodeSPrint (Name, sizeof (Name), L"Boot%04x", Number);
      if (EFI_ERROR (GetEfiGlobalVariable2 (Name, &Existing, &ExistingSize))) {
        break;
      }

      FreePool (Existing);
    }

    Status = gRT->SetVariable (Name, &gEfiGlobalVariableGuid, OPTION_ATTRIBUTES, OptionSize, Option);
    if (EFI_ERROR (Status)) {
      EFI_HANDLE  AppImage;
      EFI_STATUS  LoadStatus;

      DEBUG ((DEBUG_ERROR, "UniXNUDxe: cannot write %s: %r, starting directly\n", Name, Status));
      AppImage   = NULL;
      LoadStatus = gBS->LoadImage (
                          TRUE,
                          ImageHandle,
                          Path,
                          (VOID *)mFirmwareMarker,
                          sizeof (mFirmwareMarker),
                          &AppImage
                          );
      DEBUG ((DEBUG_ERROR, "UniXNUDxe: direct LoadImage %r\n", LoadStatus));
      FreePool (Option);
      FreePool (Path);
      if (!EFI_ERROR (LoadStatus)) {
        return gBS->StartImage (AppImage, NULL, NULL);
      }
      return Status;
    }

    DEBUG ((
      DEBUG_ERROR,
      "UniXNUDxe: %S registered, option %u bytes, data unixnu:firmware\n",
      Name,
      (UINT32)OptionSize
      ));
  }

  FreePool (Option);

  //
  // BootOrder = Number, then everything else in its old order.
  //
  NewOrder = AllocatePool (OrderSize + sizeof (UINT16));
  if (NewOrder == NULL) {
    FreePool (Path);
    return EFI_OUT_OF_RESOURCES;
  }

  NewOrder[0] = Number;
  Out         = 1;
  for (Index = 0; Index < OrderSize / sizeof (UINT16); Index++) {
    if (Order[Index] != Number) {
      NewOrder[Out++] = Order[Index];
    }
  }

  Status = gRT->SetVariable (EFI_BOOT_ORDER_VARIABLE_NAME, &gEfiGlobalVariableGuid, OPTION_ATTRIBUTES, Out * sizeof (UINT16), NewOrder);
  DEBUG ((DEBUG_INFO, "UniXNUDxe: Boot%04x first in BootOrder: %r\n", Number, Status));

  FreePool (NewOrder);
  if (Order != NULL) {
    FreePool (Order);
  }

  //
  // Do not depend on this platform's BDS handle database. Mu returns no
  // handles on NX741J, so BDS falls through to MsBootPolicy and never reaches
  // the UniXNU Boot#### we just registered. Start the application directly,
  // while still keeping the Boot#### as a fallback.
  //
  AppImage  = NULL;
  AppStatus = gBS->LoadImage (
                     TRUE,
                     ImageHandle,
                     Path,
                     (VOID *)mFirmwareMarker,
                     sizeof (mFirmwareMarker),
                     &AppImage
                     );
  if (!EFI_ERROR (AppStatus)) {
    Status = gBS->StartImage (AppImage, NULL, NULL);
  } else {
    Status = AppStatus;
  }

  FreePool (Path);
  return Status;
}
