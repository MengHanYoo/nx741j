/** @file
  Publish the TLMM MMIO window described by the device tree so DXE maps it.

  The stock DTB declares pinctrl@f000000 (qcom,canoe-tlmm) with a 0x202000 byte
  register window.  Only the first 64 KiB ends up mapped when the firmware is
  entered through the efisp loader, so key drivers cannot reach the volume /
  focus / shoot pins.  This PEIM adds the whole window as a memory-mapped IO
  resource HOB before DXE builds its GCD, which makes DxeCore map it.
**/

#include <PiPei.h>
#include <Library/PeiServicesLib.h>
#include <Library/HobLib.h>
#include <Library/DebugLib.h>

#define TLMM_BASE  0x0F000000ULL
#define TLMM_SIZE  0x202000ULL

EFI_STATUS
EFIAPI
TlmmPeiEntry (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_MAPPED_IO,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE,
    TLMM_BASE,
    TLMM_SIZE
    );

  BuildMemoryAllocationHob (TLMM_BASE, TLMM_SIZE, EfiMemoryMappedIO);

  DEBUG ((DEBUG_ERROR, "TLMMPEI: published MMIO 0x%lx-0x%lx\n", TLMM_BASE, TLMM_BASE + TLMM_SIZE));
  return EFI_SUCCESS;
}
