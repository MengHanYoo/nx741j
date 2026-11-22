/** @file
  Record whether the NX741J storage stack published a usable BlockIo disk.
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define PROBE_LBA    4764202ULL
#define PROBE_STATUS 0x424C4F434B503031ULL

STATIC UINT8 mRecord[4096] __attribute__((aligned(4096)));

EFI_STATUS
EFIAPI
BlockProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             *Handles;
  EFI_BLOCK_IO_PROTOCOL  *Bio;
  EFI_BLOCK_IO_PROTOCOL  *Best;
  UINTN                  Count;
  UINTN                  Index;
  UINT64                 Lba;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Handles = NULL;
  Best    = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiBlockIoProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  for (Index = 0; Index < Count; Index++) {
    Bio = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&Bio)) ||
        (Bio == NULL) || (Bio->Media == NULL) || !Bio->Media->MediaPresent ||
        (Bio->Media->BlockSize < 512) || (Bio->Media->BlockSize > 4096)) {
      continue;
    }
    if ((Best == NULL) || (Bio->Media->LastBlock > Best->Media->LastBlock)) {
      Best = Bio;
    }
  }
  FreePool (Handles);
  if (Best == NULL) {
    return EFI_SUCCESS;
  }

  ZeroMem (mRecord, sizeof (mRecord));
  *(UINT32 *)(mRecord + 0) = 0x32444244;
  *(UINT32 *)(mRecord + 4) = 13;
  *(UINT64 *)(mRecord + 8) = PROBE_STATUS;
  *(UINT64 *)(mRecord + 16) = Best->Media->LastBlock * Best->Media->BlockSize;
  *(UINT64 *)(mRecord + 24) = Best->Media->BlockSize;
  *(UINT64 *)(mRecord + 32) = Count;
  Lba = (PROBE_LBA * 4096ULL) / Best->Media->BlockSize;
  (VOID)Best->WriteBlocks (
              Best,
              Best->Media->MediaId,
              Lba,
              sizeof (mRecord),
              mRecord
              );
  return EFI_SUCCESS;
}
