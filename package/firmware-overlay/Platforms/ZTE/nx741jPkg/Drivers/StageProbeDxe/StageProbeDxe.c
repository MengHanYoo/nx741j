/** @file
  Persist early DXE and BlockIo publication stages in reserved RAM.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define STAGE_BASE   0xC6D02000ULL
#define STAGE_MAGIC  0x3154475453584EULL
#define STAGE_TAG    0x5354475032303236ULL

STATIC EFI_EVENT  mBlockIoEvent;

STATIC
VOID
EFIAPI
BlockIoNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  volatile UINT64  *Stage;

  (VOID)Event;
  (VOID)Context;
  Stage    = (volatile UINT64 *)(UINTN)STAGE_BASE;
  Stage[4] = 2;
  Stage[5] = STAGE_TAG;
  Stage[6] = 0x424C4F434B494F31ULL;
}

EFI_STATUS
EFIAPI
StageProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  volatile UINT64  *Stage;
  EFI_STATUS       Status;
  VOID             *Registration;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Stage    = (volatile UINT64 *)(UINTN)STAGE_BASE;
  Stage[0] = STAGE_MAGIC;
  Stage[1] = 1;
  Stage[2] = STAGE_TAG;
  Stage[3] = 0x4458455354414745ULL;

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  BlockIoNotify,
                  NULL,
                  &mBlockIoEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Registration = NULL;
  return gBS->RegisterProtocolNotify (
                &gEfiBlockIoProtocolGuid,
                mBlockIoEvent,
                &Registration
                );
}
