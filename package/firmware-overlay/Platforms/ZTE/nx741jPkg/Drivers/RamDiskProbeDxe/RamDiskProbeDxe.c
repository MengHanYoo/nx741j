/** @file
  Record whether UniXNURamDiskDxe published the loader payload as BlockIo.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define PROBE_BASE 0xC6D03000ULL

EFI_STATUS
EFIAPI
RamDiskProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  volatile UINT64        *Stage;
  EFI_STATUS             Status;
  EFI_BLOCK_IO_PROTOCOL  *Bio;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Stage  = (volatile UINT64 *)(UINTN)PROBE_BASE;
  Bio    = NULL;
  Status = gBS->LocateProtocol (&gEfiBlockIoProtocolGuid, NULL, (VOID **)&Bio);
  Stage[0] = 0x3154475453584EULL;
  Stage[1] = 1;
  Stage[2] = 0x5354475032303236ULL;
  Stage[3] = 0x52414D4449534B31ULL;
  Stage[4] = Status;
  Stage[5] = (UINT64)(UINTN)Bio;
  if ((Bio != NULL) && (Bio->Media != NULL)) {
    Stage[6] = Bio->Media->LastBlock;
    Stage[7] = Bio->Media->BlockSize;
    Stage[8] = Bio->Media->MediaPresent;
    Stage[9] = Bio->Media->ReadOnly;
  } else {
    Stage[6] = 0;
    Stage[7] = 0;
    Stage[8] = 0;
    Stage[9] = 0;
  }
  return EFI_SUCCESS;
}
