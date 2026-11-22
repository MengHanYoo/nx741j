/** @file
  DXE marker after CPU/GIC/timer dispatch.
**/

#include <Uefi.h>

EFI_STATUS
EFIAPI
PostGicProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  volatile UINT64  *Stage;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Stage    = (volatile UINT64 *)(UINTN)0xC6D01000ULL;
  Stage[0] = 0x3154475453584EULL;
  Stage[1] = 1;
  Stage[2] = 0x5354475032303236ULL;
  Stage[3] = 0x54494D4552504F53ULL;
  return EFI_SUCCESS;
}
