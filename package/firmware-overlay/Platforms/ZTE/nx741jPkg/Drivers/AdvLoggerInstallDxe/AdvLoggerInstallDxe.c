/** @file Installs the Advanced Logger protocol through AdvancedLoggerLib's constructor. */
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS
EFIAPI
AdvLoggerInstallEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  (VOID)ImageHandle;
  (VOID)SystemTable;
  return EFI_SUCCESS;
}
