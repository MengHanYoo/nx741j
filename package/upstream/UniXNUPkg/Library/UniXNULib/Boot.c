/** @file
  Entry execution.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Internal.h"

#include <Guid/Fdt.h>
#include <Protocol/UniXNULoader.h>

STATIC
EFI_STATUS
RunAction (
  IN UNIXNU_ENTRY  *Entry
  )
{
  switch (Entry->Action) {
    case UniXNUActionReset:
      UniXNULog ("action: reset");
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
      break;
    case UniXNUActionShutdown:
      UniXNULog ("action: shutdown");
      gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
      break;
    case UniXNUActionExit:
      UniXNULog ("action: exit");
      return EFI_SUCCESS;
    default:
      break;
  }

  return EFI_UNSUPPORTED;
}

//
// Seam for Darwin-style payloads; see Docs/Boundaries.md.
//
STATIC
EFI_STATUS
RunXnu (
  IN UNIXNU_CONFIG *Config,
  IN UNIXNU_ENTRY  *Entry,
  IN VOID      *Fdt,
  IN UINTN     FdtSize,
  IN VOID      *Ramdisk,
  IN UINTN     RamdiskSize
  )
{
  EFI_STATUS                     Status;
  UNIXNU_LOADER_PROTOCOL  *Loader;
  UNIXNU_LOADER_BOOT_REQUEST     Request;
  UNIXNU_LOADER_KEXT             *Kexts;
  UINTN                         KextCount;

  DEBUG ((DEBUG_ERROR, "UNIXNU: RunXnu entered, config %lx entry %lx\n",
          (UINT64)(UINTN)Config, (UINT64)(UINTN)Entry));
  Print (L"UNIXNU: RunXnu entered\r\n");
  gBS->Stall (1000 * 1000);
  DEBUG ((DEBUG_ERROR, "UNIXNU: looking for the loader protocol\n"));
  Status = gBS->LocateProtocol (&gUniXNULoaderProtocolGuid, NULL, (VOID **)&Loader);
  DEBUG ((DEBUG_ERROR, "UNIXNU: loader locate -> %r, pointer %lx\n", Status, (UINT64)(UINTN)Loader));
  Print (L"UNIXNU: loader pointer\r\n");
  gBS->Stall (2000 * 1000);
  if (EFI_ERROR (Status)) {
    UniXNULog ("xnu: no loader protocol installed; UniXNU does not provide one");
    return EFI_UNSUPPORTED;
  }

  if (((Loader->Revision != UNIXNU_LOADER_REVISION_1) &&
       (Loader->Revision != UNIXNU_LOADER_REVISION)) || (Loader->Boot == NULL))
  {
    UniXNULog ("xnu: unsupported loader revision or missing Boot callback");
    return EFI_UNSUPPORTED;
  }

  Print (L"UNIXNU: reading loader fields\r\n");
  gBS->Stall (1000 * 1000);
  DEBUG ((DEBUG_ERROR, "UNIXNU: loader revision %lx boot %lx\n",
          (UINT64)Loader->Revision, (UINT64)(UINTN)Loader->Boot));
  Print (L"UNIXNU: loader fields ok\r\n");
  DEBUG ((DEBUG_ERROR, "UNIXNU: kext count %lu config-error %u\n",
          (UINT64)Entry->KextCount, (UINT32)Entry->KextConfigError));
  if (Entry->KextCount == 0) {
    Kexts      = NULL;
    KextCount  = 0;
    Status     = EFI_SUCCESS;
    DEBUG ((DEBUG_ERROR, "UNIXNU: no kexts configured, skipping staging\n"));
    Print (L"UNIXNU: no kexts configured\r\n");
  } else {
    DEBUG ((DEBUG_ERROR, "UNIXNU: staging %lu kexts\n", (UINT64)Entry->KextCount));
    Print (L"UNIXNU: staging kexts\r\n");
    Status = UniXNUStageKexts (Config, Entry, &Kexts, &KextCount);
  }
  DEBUG ((DEBUG_ERROR, "UNIXNU: kexts -> %r count %lu\n", Status, (UINT64)KextCount));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((KextCount != 0) && (Loader->Revision == UNIXNU_LOADER_REVISION_1)) {
    UniXNULog ("xnu: loader revision 1 cannot accept kexts");
    UniXNUFreeKexts (Kexts, KextCount);
    return EFI_UNSUPPORTED;
  }

  ZeroMem (&Request, sizeof (Request));
  Request.Volume         = Entry->Volume;
  Request.KernelPath     = Entry->Path;
  Request.BootArgs       = Entry->Args;
  Request.DeviceTree     = Fdt;
  Request.DeviceTreeSize = FdtSize;
  Request.Ramdisk        = Ramdisk;
  Request.RamdiskSize    = RamdiskSize;
  Request.Kexts          = Kexts;
  Request.KextCount      = KextCount;
  DEBUG ((DEBUG_ERROR, "UNIXNU: handing off, kexts %lu\n", (UINT64)KextCount));
  Print (L"UNIXNU: handing off to loader\r\n");
  UniXNULog ("xnu: handing off %u staged kext(s) to loader", (UINT32)KextCount);
  gBS->Stall (1000 * 1000);
  DEBUG ((DEBUG_ERROR, "UNIXNU: calling Loader->Boot %lx\n", (UINT64)(UINTN)Loader->Boot));
  Print (L"UNIXNU: calling Loader->Boot\r\n");
  gBS->Stall (1000 * 1000);
  Status = Loader->Boot (Loader, &Request);
  DEBUG ((DEBUG_ERROR, "UNIXNU: Loader->Boot returned %r\n", Status));
  UniXNUFreeKexts (Kexts, KextCount);
  return Status;
}

EFI_STATUS
UniXNUBootEntry (
  IN EFI_HANDLE  ImageHandle,
  IN UNIXNU_CONFIG   *Config,
  IN UNIXNU_ENTRY    *Entry
  )
{
  EFI_STATUS                 Status;
  VOID                       *Fdt;
  UINTN                      FdtSize;
  VOID                       *Initrd;
  UINTN                      InitrdSize;
  EFI_HANDLE                 InitrdHandle;
  EFI_DEVICE_PATH_PROTOCOL   *Path;
  EFI_HANDLE                 Image;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;
  UINTN                      ExitDataSize;
  CHAR16                     *ExitData;
  VOID                       *FirmwareFdt;

  DEBUG ((DEBUG_ERROR, "UNIXNU: BootEntry entered, entry %lx config %lx\n",
          (UINT64)(UINTN)Entry, (UINT64)(UINTN)Config));
  Print (L"UNIXNU: BootEntry entered\r\n");
  gBS->Stall (5 * 1000 * 1000);
  DEBUG ((DEBUG_ERROR, "UNIXNU: entry id %lx title %lx\n",
          (UINT64)(UINTN)(Entry->Id), (UINT64)(UINTN)(Entry->Title)));
  UniXNULog ("boot: entry '%a' (%s)", (Entry->Id != NULL) ? Entry->Id : "?", Entry->Title);
  DEBUG ((DEBUG_ERROR, "UNIXNU: first log done\n"));

  if (Entry->KextConfigError) {
    UniXNULog ("kext: invalid entry configuration; boot refused");
    return EFI_INVALID_PARAMETER;
  }

  if (Entry->Type == UniXNUEntryAction) {
    return RunAction (Entry);
  }

  Fdt          = NULL;
  FdtSize      = 0;
  Initrd       = NULL;
  InitrdSize   = 0;
  InitrdHandle = NULL;

  if (Entry->Type == UniXNUEntryXnu) {
    DEBUG ((DEBUG_ERROR, "UNIXNU: XNU entry, skipping UEFI variable writes\n"));
    Print (L"UNIXNU: XNU entry, variables skipped\r\n");
  } else {
    DEBUG ((DEBUG_ERROR, "UNIXNU: setting entry variables\n"));
    UniXNUSetEntryVariables (Entry);
    DEBUG ((DEBUG_ERROR, "UNIXNU: entry variables set\n"));
  }
  gBS->Stall (1000 * 1000);

  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gFdtTableGuid, &FirmwareFdt))) {
    FirmwareFdt = NULL;
  }

  Status = UniXNUPrepareDeviceTree (Config, Entry, &Fdt, &FdtSize);
  DEBUG ((DEBUG_ERROR, "UNIXNU: device tree prepared -> %r (fdt %lx size %lu)\n", Status, (UINT64)(UINTN)Fdt, (UINT64)FdtSize));
  gBS->Stall (1000 * 1000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_ERROR, "UNIXNU: after DT, initrd %lx volume %lx path %lx\n",
          (UINT64)(UINTN)Entry->Initrd, (UINT64)(UINTN)Entry->Volume, (UINT64)(UINTN)Entry->Path));
  Print (L"UNIXNU: after device tree\r\n");
  gBS->Stall (1000 * 1000);
  if (Entry->Initrd != NULL) {
    Print (L"UNIXNU: reading initrd\r\n");
    Status = UniXNUReadFile (Entry->Volume, Entry->Initrd, &Initrd, &InitrdSize);
    DEBUG ((DEBUG_ERROR, "UNIXNU: initrd read -> %r size %lu\n", Status, (UINT64)InitrdSize));
    if (EFI_ERROR (Status)) {
      UniXNULog ("boot: cannot read initrd %s: %r", Entry->Initrd, Status);
      goto Done;
    }

    if (Entry->Type != UniXNUEntryXnu) {
      Status = UniXNUInstallInitrd (Initrd, InitrdSize, &InitrdHandle);
      if (EFI_ERROR (Status)) {
        UniXNULog ("boot: cannot publish initrd: %r", Status);
        goto Done;
      }

      UniXNULog ("boot: initrd %s, %u bytes on LoadFile2 initrd media", Entry->Initrd, (UINT32)InitrdSize);
    }
  }

  if (Entry->Type == UniXNUEntryXnu) {
    Status = RunXnu (Config, Entry, Fdt, FdtSize, Initrd, InitrdSize);
    goto Done;
  }

  //
  // efi and linux: a Linux kernel with the EFI stub is an EFI image whose
  // command line is its LoadOptions; initrd and DTB are published above.
  //
  Path = UniXNUResolveImagePath (Entry->Volume, Entry->Path);
  if (Path == NULL) {
    UniXNULog ("boot: cannot resolve %s", Entry->Path);
    Status = EFI_NOT_FOUND;
    goto Done;
  }

  Image  = NULL;
  Status = gBS->LoadImage (FALSE, ImageHandle, Path, NULL, 0, &Image);
  FreePool (Path);
  if (EFI_ERROR (Status)) {
    if ((Status == EFI_SECURITY_VIOLATION) && (Image != NULL)) {
      gBS->UnloadImage (Image);
    }

    UniXNULog ("boot: LoadImage %s: %r", Entry->Path, Status);
    goto Done;
  }

  if ((Entry->Args != NULL) &&
      !EFI_ERROR (gBS->HandleProtocol (Image, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded)))
  {
    Loaded->LoadOptions     = Entry->Args;
    Loaded->LoadOptionsSize = (UINT32)StrSize (Entry->Args);
  }

  UniXNULog ("boot: starting %s", Entry->Path);
  ExitData     = NULL;
  ExitDataSize = 0;
  Status       = gBS->StartImage (Image, &ExitDataSize, &ExitData);
  UniXNULog ("boot: %s returned %r", Entry->Path, Status);
  if (ExitData != NULL) {
    FreePool (ExitData);
  }

Done:
  //
  // The payload came back: put the firmware's own tree back so the next
  // entry starts from the same state.
  //
  if (Fdt != NULL) {
    gBS->InstallConfigurationTable (&gFdtTableGuid, FirmwareFdt);
  }

  UniXNUUninstallInitrd (InitrdHandle);
  if (Initrd != NULL) {
    FreePool (Initrd);
  }

  return Status;
}
