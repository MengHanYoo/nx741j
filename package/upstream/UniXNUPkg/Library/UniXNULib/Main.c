/** @file
  UniXNURun() and logging.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Internal.h"
#include <Protocol/BlockIo.h>
extern VOID UniXNUProgress (UINT32 Step, UINT64 Status);

// ---- boot progress record (rawdump + 79 MiB) -------------------------------
/* The efisp loader records its pre-jump step at +79 MiB. Keep the firmware
 * progress in the next sector so the next loader run cannot overwrite it.
 *
 * Keep this path deliberately small: the firmware has already handed storage
 * over from ABL, and probing every BlockIo handle can stall before the menu is
 * visible on SM8850. The whole-disk handle has the greatest media size and its
 * LBAs are absolute, so use it directly. */
#define BP_ABSOLUTE_LBA  4764201ULL
typedef struct { UINT32 Magic; UINT32 Step; UINT64 Status; UINT64 A; UINT64 B; UINT64 C; } BP_RECORD;
STATIC BP_RECORD mBp;
STATIC __attribute__((aligned(4096))) UINT8 mBpSector[4096];

STATIC EFI_STATUS
FindProgressDisk (
  OUT EFI_BLOCK_IO_PROTOCOL  **FoundBio,
  OUT UINT32                 *FoundBlockSize
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             *Handles = NULL;
  EFI_BLOCK_IO_PROTOCOL  *Bio;
  EFI_BLOCK_IO_PROTOCOL  *Best = NULL;
  UINTN                  Count = 0;
  UINTN                  Index;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  for (Index = 0; Index < Count; Index++) {
    Bio = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&Bio)) ||
        (Bio == NULL) || (Bio->Media == NULL) ||
        !Bio->Media->MediaPresent ||
        (Bio->Media->BlockSize < 512) || (Bio->Media->BlockSize > 4096)) {
      continue;
    }
    if ((Best == NULL) || (Bio->Media->LastBlock > Best->Media->LastBlock)) {
      Best = Bio;
    }
  }
  FreePool (Handles);
  if (Best == NULL) {
    return EFI_NOT_FOUND;
  }
  *FoundBio = Best;
  *FoundBlockSize = Best->Media->BlockSize;
  return EFI_SUCCESS;
}

VOID
UniXNUProgress (
  IN UINT32  Step,
  IN UINT64  Status
  )
{
  EFI_STATUS             WriteStatus;
  EFI_BLOCK_IO_PROTOCOL  *Bio = NULL;
  UINT32                 BlockSize = 0;
  UINT64                 Lba;

  ZeroMem (mBpSector, sizeof (mBpSector));
  mBp.Magic = 0x32444244;
  mBp.Step = Step;
  mBp.Status = Status;
  CopyMem (mBpSector, &mBp, sizeof (mBp));
  if (EFI_ERROR (FindProgressDisk (&Bio, &BlockSize))) {
    return;
  }
  Lba = (BP_ABSOLUTE_LBA * 4096ULL) / BlockSize;
  WriteStatus = Bio->WriteBlocks (
                         Bio,
                         Bio->Media->MediaId,
                         Lba,
                         sizeof (mBpSector),
                         mBpSector
                         );
  (VOID)WriteStatus;
}
// ---------------------------------------------------------------------------


#include <Protocol/SerialIo.h>

STATIC EFI_SERIAL_IO_PROTOCOL  *mSerial;

VOID
UniXNULogInit (
  VOID
  )
{
  if (EFI_ERROR (gBS->LocateProtocol (&gEfiSerialIoProtocolGuid, NULL, (VOID **)&mSerial))) {
    mSerial = NULL;
  }
}

VOID
EFIAPI
UniXNULog (
  IN CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Args;
  CHAR8    Line[256];
  UINTN    Prefix;
  UINTN    Size;

  Prefix = AsciiSPrint (Line, sizeof (Line), "UNIXNU: ");
  VA_START (Args, Format);
  AsciiVSPrint (Line + Prefix, sizeof (Line) - Prefix - 2, Format, Args);
  VA_END (Args);
  AsciiStrCatS (Line, sizeof (Line), "\r\n");

  DEBUG ((PcdGet32 (PcdUniXNUDebugLevel), "%a", Line));
  if (mSerial != NULL) {
    Size = AsciiStrLen (Line);
    mSerial->Write (mSerial, &Size, Line);
  }
}

EFI_STATUS
EFIAPI
UniXNURun (
  IN EFI_HANDLE      ImageHandle,
  IN UNIXNU_HOST  Host
  )
{
  EFI_STATUS       Status;
  UNIXNU_CONFIG        Config;
  UNIXNU_UI            Ui;
  UNIXNU_INJECT_STATS  Stats;
  UINTN            Index;
  BOOLEAN          Countdown;
  UNIXNU_ENTRY         *Entry;
  UNIXNU_ENTRY         BootCopy;

  UniXNUProgress (16, (UINT64)Host);

  //
  // A boot manager waits for people; the 5 minute BDS watchdog does not.
  //
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  UniXNUProgress (17, 0);
  UniXNULogInit ();
  UniXNUProgress (18, 0);
  UniXNULog (
    "UniXNU %a starting as %a, firmware %s rev 0x%x",
    UNIXNU_VERSION_STRING,
    (Host == UniXNUHostFirmware) ? "firmware boot option" : "application",
    (gST->FirmwareVendor != NULL) ? gST->FirmwareVendor : L"?",
    gST->FirmwareRevision
    );

  //
  // Started straight from BDS, storage may not be connected yet.
  //
  if (Host == UniXNUHostFirmware) {
    UniXNUConnectAll ();
  }
  UniXNUProgress (19, 0);
  UniXNUProgress (20, 0);

  UniXNUConfigLoad (ImageHandle, &Config);
  UniXNUProgress (21, (UINT64)Config.EntryCount);
  UniXNULog (
    "config: %u entries, %u drivers, %u dt patches, %u acpi tables, %u slices, %u kits loaded, %u skipped",
    (UINT32)Config.EntryCount,
    (UINT32)Config.DriverCount,
    (UINT32)Config.PatchCount,
    (UINT32)Config.AcpiCount,
    (UINT32)Config.SliceCount,
    (UINT32)Config.KitsLoaded,
    (UINT32)Config.KitsSkipped
    );

  UniXNUPublishSlices (&Config);

  //
  // Drivers before the UI: an injected driver may provide the display or
  // the input device the menu needs.
  //
  ZeroMem (&Stats, sizeof (Stats));
  UniXNUInjectDrivers (ImageHandle, &Config, &Stats);
  UniXNUInjectAcpi (&Config, &Stats);
  UniXNUProgress (22, (UINT64)Stats.DriversLoaded);

  UniXNUUiInit (&Config, &Ui);
  UniXNUProgress (23, 0);

  UnicodeSPrint (
    Ui.Info,
    sizeof (Ui.Info),
    L"drivers %u loaded / %u failed    kits %u    acpi %u",
    (UINT32)Stats.DriversLoaded,
    (UINT32)Stats.DriversFailed,
    (UINT32)Config.KitsLoaded,
    (UINT32)Stats.AcpiInstalled
    );

  Countdown = TRUE;
  UniXNUProgress (30, 0);
  for ( ; ;) {
    Index  = UniXNUMenuRun (&Ui, &Config, Countdown);
    UniXNUProgress (31, (UINT64)Index);
    Entry  = &Config.Entries[Index];
    BootCopy = *Entry;
    if (Entry->Confirm != NULL) {
      UINTN  Choice;

      if (!UniXNUMenuConfirm (&Ui, Entry, &Choice)) {
        UniXNULog ("menu: entry %u not confirmed", (UINT32)Index);
        UnicodeSPrint (Ui.Notice, sizeof (Ui.Notice), L"'%s' cancelled", Entry->Title);
        Countdown = FALSE;
        continue;
      }

      if (Entry->ConfirmCount > 0) {
        //
        // The chosen option's arguments follow the entry's own, on a copy: the entry itself stays as configured.
        //
        UINTN  Length = (Entry->Args != NULL ? StrLen (Entry->Args) : 0) + 1 + StrLen (Entry->ConfirmArgs[Choice]) + 1;

        BootCopy.Args = AllocateZeroPool (Length * sizeof (CHAR16));
        if (BootCopy.Args != NULL) {
          UnicodeSPrint (BootCopy.Args, Length * sizeof (CHAR16), L"%s %s", Entry->Args != NULL ? Entry->Args : L"", Entry->ConfirmArgs[Choice]);
        } else {
          BootCopy.Args = Entry->Args;
        }
      }
    }

    if ((Entry->Type == UniXNUEntryAction) && (Entry->Action == UniXNUActionTools)) {
      UniXNUMenuRunTools (&Ui, &Config);
      Countdown = FALSE;
      continue;
    }

    Status = UniXNUBootEntry (ImageHandle, &Config, &BootCopy);
    if ((Entry->Type == UniXNUEntryAction) && (Entry->Action == UniXNUActionExit)) {
      return EFI_SUCCESS;
    }

    //
    // The payload returned or failed: back to the menu, without the
    // countdown so a broken default cannot loop on a machine with input.
    //
    UnicodeSPrint (Ui.Notice, sizeof (Ui.Notice), L"'%s' returned: %r", Entry->Title, Status);
    UniXNULog ("menu: entry %u returned %r", (UINT32)Index, Status);
    gBS->SetWatchdogTimer (0, 0, 0, NULL);
    Countdown = FALSE;
  }
}
