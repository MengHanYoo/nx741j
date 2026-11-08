/** @file
  Dispatch-order probe: prints NX741J STOP and spins forever.

  Placed at a chosen APRIORI position it turns "which driver crashes" into a
  visible yes/no: the marker means everything before this point dispatched.
**/
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/BaseLib.h>

STATIC CONST CHAR16 mMarker[] = L"NX741J STOP\r\n";

EFI_STATUS
EFIAPI
StopDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  if (gST->ConOut != NULL) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *)mMarker);
  }
  for (;;) {
    CpuDeadLoop ();
  }
  return EFI_SUCCESS;
}
