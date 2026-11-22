/** @file
  First DXE dispatch marker after core/PCD.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS
EFIAPI
EarlyProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  volatile UINT64  *Stage;
  EFI_PHYSICAL_ADDRESS Probes;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Probes = 0xC6D00000ULL;
  (VOID)gBS->AllocatePages (AllocateAddress, EfiReservedMemoryType, 4, &Probes);
  Stage    = (volatile UINT64 *)(UINTN)0xC6D00000ULL;
  Stage[0] = 0x3154475453584EULL;
  Stage[1] = 1;
  Stage[2] = 0x5354475032303236ULL;
  Stage[3] = 0x4458454D41494E31ULL;
  return EFI_SUCCESS;
}
