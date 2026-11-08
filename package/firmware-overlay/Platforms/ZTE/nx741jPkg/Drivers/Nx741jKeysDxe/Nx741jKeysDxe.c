/** @file
  TLMM mapping probe: print the GCD descriptors, then read one dword per page
  boundary and print each value before the next read. No writes, no gDS changes.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/DebugLib.h>

#define TLMM_BASE  0x0F000000ULL

STATIC
UINT32
Rd (
  IN UINT64  Address
  )
{
  return *(volatile UINT32 *)(UINTN)Address;
}

EFI_STATUS
EFIAPI
Nx741jKeysDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Desc;

  Status = gDS->GetMemorySpaceDescriptor (TLMM_BASE, &Desc);
  DEBUG ((DEBUG_ERROR, "KEYS: gcd 0x0F000000 -> %r type %u len 0x%lx attr 0x%lx\n",
          Status, Status == EFI_SUCCESS ? (UINT32)Desc.GcdMemoryType : 0xFF,
          Status == EFI_SUCCESS ? (UINT64)Desc.Length : 0ULL,
          Status == EFI_SUCCESS ? (UINT64)Desc.Attributes : 0ULL));

  Status = gDS->GetMemorySpaceDescriptor (TLMM_BASE + 0x10000, &Desc);
  DEBUG ((DEBUG_ERROR, "KEYS: gcd 0x0F010000 -> %r type %u len 0x%lx attr 0x%lx\n",
          Status, Status == EFI_SUCCESS ? (UINT32)Desc.GcdMemoryType : 0xFF,
          Status == EFI_SUCCESS ? (UINT64)Desc.Length : 0ULL,
          Status == EFI_SUCCESS ? (UINT64)Desc.Attributes : 0ULL));

  DEBUG ((DEBUG_ERROR, "KEYS: read 0x0F000000\n"));
  DEBUG ((DEBUG_ERROR, "KEYS: value 0x%08x\n", Rd (TLMM_BASE)));
  gBS->Stall (2 * 1000 * 1000);

  DEBUG ((DEBUG_ERROR, "KEYS: read 0x0F00FFF0\n"));
  DEBUG ((DEBUG_ERROR, "KEYS: value 0x%08x\n", Rd (TLMM_BASE + 0xFFF0)));
  gBS->Stall (2 * 1000 * 1000);

  DEBUG ((DEBUG_ERROR, "KEYS: read 0x0F010000\n"));
  DEBUG ((DEBUG_ERROR, "KEYS: value 0x%08x\n", Rd (TLMM_BASE + 0x10000)));
  gBS->Stall (2 * 1000 * 1000);

  DEBUG ((DEBUG_ERROR, "KEYS: read 0x0F065000 (vol+)\n"));
  DEBUG ((DEBUG_ERROR, "KEYS: value 0x%08x\n", Rd (TLMM_BASE + 0x65000)));
  gBS->Stall (10 * 1000 * 1000);
  return EFI_SUCCESS;
}
