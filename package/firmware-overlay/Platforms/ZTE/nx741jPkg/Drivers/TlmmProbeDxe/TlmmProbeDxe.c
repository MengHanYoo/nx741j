/** @file
  NX741J bring-up probe: dumps the TLMM register window and the registered
  Boot#### load option. Runs after the console drivers so the output is visible.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Guid/GlobalVariable.h>
#include <Library/DebugLib.h>

#define TLMM_BASE  0x0F000000ULL

STATIC
UINT32
MmioRead32 (
  IN UINT64  Address
  )
{
  return *(volatile UINT32 *)(UINTN)Address;
}

STATIC
VOID
DumpBootOption (
  VOID
  )
{
  UINT16      *Order;
  UINTN        OrderSize;
  CHAR16       Name[16];
  UINT8       *Option;
  UINTN        OptionSize;
  EFI_STATUS   Status;

  OrderSize = 0;
  Status    = gRT->GetVariable (L"BootOrder", &gEfiGlobalVariableGuid, NULL, &OrderSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((DEBUG_ERROR, "PROBE: no BootOrder (%r)\n", Status));
    return;
  }

  Order = AllocatePool (OrderSize);
  if (Order == NULL) {
    return;
  }

  Status = gRT->GetVariable (L"BootOrder", &gEfiGlobalVariableGuid, NULL, &OrderSize, Order);
  if (EFI_ERROR (Status) || (OrderSize < sizeof (UINT16))) {
    DEBUG ((DEBUG_ERROR, "PROBE: BootOrder read failed (%r)\n", Status));
    FreePool (Order);
    return;
  }

  UnicodeSPrint (Name, sizeof (Name), L"Boot%04x", Order[0]);
  DEBUG ((DEBUG_ERROR, "PROBE: BootOrder[0] = %S\n", Name));

  OptionSize = 0;
  Status     = gRT->GetVariable (Name, &gEfiGlobalVariableGuid, NULL, &OptionSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Option = AllocatePool (OptionSize);
    if (Option != NULL) {
      Status = gRT->GetVariable (Name, &gEfiGlobalVariableGuid, NULL, &OptionSize, Option);
      if (!EFI_ERROR (Status) && (OptionSize > 6)) {
        UINT16  PathLen = *(UINT16 *)(Option + 4);
        CHAR16  *Desc   = (CHAR16 *)(Option + 6);
        UINTN   DescLen = StrLen (Desc) + 1;
        UINTN   OptLen  = OptionSize - 6 - DescLen * sizeof (CHAR16) - PathLen;

        DEBUG ((
          DEBUG_ERROR,
          "PROBE: %S option %u B, desc '%S', filepath %u B, optional %u B\n",
          Name,
          (UINT32)OptionSize,
          Desc,
          PathLen,
          (UINT32)OptLen
          ));
      }

      FreePool (Option);
    }
  }

  FreePool (Order);
}

EFI_STATUS
EFIAPI
TlmmProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT64  Offsets[] = { 0x0, 0x4, 0x8, 0x1000, 0x1004, 0x2000, 0x2004,
                        0x10000, 0x10004, 0x20000, 0x20004, 0x64000, 0x64004 };
  UINTN   Index;

  DEBUG ((DEBUG_ERROR, "TLMMPROBE: base 0x%lx\n", TLMM_BASE));
  for (Index = 0; Index < ARRAY_SIZE (Offsets); Index++) {
    DEBUG ((
      DEBUG_ERROR,
      "TLMMPROBE: +0x%05lx = 0x%08x\n",
      Offsets[Index],
      MmioRead32 (TLMM_BASE + Offsets[Index])
      ));
  }

  DumpBootOption ();

  DEBUG ((DEBUG_ERROR, "TLMMPROBE: holding 15s for capture\n"));
  gBS->Stall (15 * 1000 * 1000);

  return EFI_SUCCESS;
}
