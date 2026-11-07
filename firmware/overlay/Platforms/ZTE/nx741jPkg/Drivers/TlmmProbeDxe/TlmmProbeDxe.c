/** @file
  Read-only TLMM probe for the NX741J key lines.

  The device tree maps volume-up to TLMM GPIO 101 (active low), focus to 179
  and shoot to 95.  This driver only reads the TLMM window so the register
  layout can be confirmed on hardware before a key driver writes anything.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
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

EFI_STATUS
EFIAPI
TlmmProbeDxeEntry (
  VOID
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

  return EFI_SUCCESS;
}
