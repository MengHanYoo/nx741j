/** @file
  SMEM stub driver for the nx741j UEFI bring-up.

  The vendor SmemDxe expects the SMEM region at 0x81D00000 to have been
  initialized by XBL.  When the FV is entered through BootShim that region
  has no UEFI page-table mapping, so SmemDxe fails in its entry point
  (ASSERT_RETURN_ERROR(status=Not Found)) and the DXE chain never reaches
  DALSYSDxe.

  This driver installs an EFI_SMEM_PROTOCOL that is backed by ordinary DXE
  boot-services pages instead.  The only consumer in this firmware is
  DALSYSDxe, which allocates SMEM item 404 (0x194) with a size of 8192 bytes
  and uses it as private zero-initialized configuration storage.  Items are
  therefore allocated lazily, zeroed, and page aligned (DALSYSDxe aligns the
  item address to 4 KiB before using it).

  This is a bring-up stub: item contents are not shared with any other
  processor and do not persist across a reboot.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/EFISmem.h>

//
// Upper bound of SMEM item indices accepted by the stub.  DALSYSDxe asks for
// item 404 (0x194), so the table must comfortably exceed that.
//
#define SMEM_STUB_MAX_ITEMS  1024

//
// Every item gets at least one page so that item addresses stay 4 KiB aligned.
//
#define SMEM_STUB_MIN_SIZE   SIZE_4KB

typedef struct {
  VOID    *Buffer;
  UINT32  Size;
} SMEM_STUB_ITEM;

STATIC EFI_SMEM_PROTOCOL  mSmemStubProtocol;
STATIC SMEM_STUB_ITEM     mSmemStubItems[SMEM_STUB_MAX_ITEMS];

/**
  Allocate (or return) the backing store of one SMEM item.

  @param[in]  ItemIndex  SMEM item index.
  @param[in]  Size       Requested size in bytes.
  @param[out] pAddr      Receives the page aligned item address.

  @retval EFI_SUCCESS            The item is backed by zeroed memory.
  @retval EFI_INVALID_PARAMETER  pAddr is NULL.
  @retval EFI_NOT_FOUND          ItemIndex is outside the stub range.
  @retval EFI_OUT_OF_RESOURCES   Pages could not be allocated.
**/
STATIC
EFI_STATUS
EFIAPI
SmemStubAllocate (
  IN  int           ItemIndex,
  IN  unsigned int  Size,
  OUT VOID          **pAddr
  )
{
  SMEM_STUB_ITEM  *Item;
  UINTN           Pages;
  VOID            *Buffer;

  if (pAddr == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *pAddr = NULL;

  if ((ItemIndex < 0) || (ItemIndex >= SMEM_STUB_MAX_ITEMS)) {
    return EFI_NOT_FOUND;
  }

  if (Size < SMEM_STUB_MIN_SIZE) {
    Size = SMEM_STUB_MIN_SIZE;
  }

  Item = &mSmemStubItems[ItemIndex];

  if ((Item->Buffer != NULL) && (Item->Size >= Size)) {
    *pAddr = Item->Buffer;
    return EFI_SUCCESS;
  }

  Pages  = EFI_SIZE_TO_PAGES ((UINTN)Size);
  Buffer = AllocatePages (Pages);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (Buffer, EFI_PAGES_TO_SIZE (Pages));

  if (Item->Buffer != NULL) {
    FreePages (Item->Buffer, EFI_SIZE_TO_PAGES ((UINTN)Item->Size));
  }

  Item->Buffer = Buffer;
  Item->Size   = (UINT32)EFI_PAGES_TO_SIZE (Pages);

  DEBUG ((
    DEBUG_ERROR,
    "SmemStub: item %d -> 0x%p (%u bytes)\n",
    ItemIndex,
    Buffer,
    Item->Size
    ));

  *pAddr = Buffer;
  return EFI_SUCCESS;
}

/**
  Return the backing store and size of one SMEM item.

  Items that were never allocated are created on demand with the minimum
  size, so that consumers never observe a NULL item address.

  @param[in]     ItemIndex  SMEM item index.
  @param[in,out] pSize      Receives the item size in bytes.
  @param[out]    pAddr      Receives the page aligned item address.

  @retval EFI_SUCCESS            The item is backed by zeroed memory.
  @retval EFI_INVALID_PARAMETER  pSize or pAddr is NULL.
  @retval EFI_NOT_FOUND          ItemIndex is outside the stub range.
  @retval EFI_OUT_OF_RESOURCES   Pages could not be allocated.
**/
STATIC
EFI_STATUS
EFIAPI
SmemStubGetAddr (
  IN     int           ItemIndex,
  IN OUT unsigned int  *pSize,
  OUT    VOID          **pAddr
  )
{
  EFI_STATUS  Status;

  if ((pSize == NULL) || (pAddr == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *pSize = 0;
  *pAddr = NULL;

  if ((ItemIndex < 0) || (ItemIndex >= SMEM_STUB_MAX_ITEMS)) {
    return EFI_NOT_FOUND;
  }

  if (mSmemStubItems[ItemIndex].Buffer == NULL) {
    Status = SmemStubAllocate (ItemIndex, SMEM_STUB_MIN_SIZE, pAddr);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  *pSize = mSmemStubItems[ItemIndex].Size;
  *pAddr = mSmemStubItems[ItemIndex].Buffer;
  return EFI_SUCCESS;
}

/**
  Placeholder for the third protocol service; unused by this firmware.
**/
STATIC
EFI_STATUS
EFIAPI
SmemStubFunc2 (
  IN VOID  *Params
  )
{
  (VOID)Params;
  return EFI_SUCCESS;
}

/**
  Placeholder for the fourth protocol service; unused by this firmware.
**/
STATIC
EFI_STATUS
EFIAPI
SmemStubFunc3 (
  IN VOID  *Params
  )
{
  (VOID)Params;
  return EFI_SUCCESS;
}

/**
  Install the stub EFI_SMEM_PROTOCOL.

  @param[in] ImageHandle  Image handle of this driver.
  @param[in] SystemTable  Pointer to the EFI System Table.

  @retval EFI_SUCCESS  The protocol was installed.
  @return              Status of InstallMultipleProtocolInterfaces().
**/
EFI_STATUS
EFIAPI
SmemStubEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Handle = NULL;

  DEBUG ((DEBUG_ERROR, "SmemStubDxe: entry (gBS=%p)\n", gBS));

  //
  // Diagnostic: check whether the real SMEM region is accessible from DXE.
  // XBL initialized it before ABL; if the page tables cover it we see the
  // SMEM header magic here, otherwise the read faults and we know the region
  // is not mapped.
  //
  {
    STATIC CONST UINTN  Probe[] = {
      0x81A00000ULL,   // XBL_DT start
      0x81A20000ULL,   // XBL_DT +128K
      0x81A40000ULL,   // XBL_DT +256K
      0x81D00000ULL,   // SMEM
      0xFFBA0000ULL    // FD_Reserved (XBL scan window)
    };
    UINTN  Index;

    for (Index = 0; Index < ARRAY_SIZE (Probe); Index++) {
      volatile UINT32  *Word;

      Word = (volatile UINT32 *)Probe[Index];
      DEBUG ((
        DEBUG_ERROR,
        "Probe %08lx: %08x %08x %08x %08x%s\n",
        Probe[Index],
        Word[0],
        Word[1],
        Word[2],
        Word[3],
        (Word[0] == 0xD00DFEED) ? " FDT" : ""
        ));
    }

    //
    // Scan the XBL DT and FD_Reserved windows for FDT headers.
    //
    {
      STATIC CONST UINTN  Window[] = { 0x81A00000ULL, 0xFFBA0000ULL };
      STATIC CONST UINTN  WindowSize[] = { 0x60000ULL, 0x100000ULL };
      UINTN               W;
      UINTN               Hit;

      for (W = 0; W < ARRAY_SIZE (Window); W++) {
        Hit = 0;
        for (Index = 0; Index < WindowSize[W] / sizeof (UINT32); Index++) {
          if (((volatile UINT32 *)Window[W])[Index] == 0xD00DFEED) {
            DEBUG ((
              DEBUG_ERROR,
              "FDT hit at %08lx (size %08x)\n",
              Window[W] + Index * 4,
              ((volatile UINT32 *)Window[W])[Index + 1]
              ));
            if (++Hit >= 6) {
              break;
            }
          }
        }

        DEBUG ((DEBUG_ERROR, "FDT scan %08lx: %u hit(s)\n", Window[W], (UINT32)Hit));
      }
    }
  }

  mSmemStubProtocol.Revision  = EFI_SMEM_PROTOCOL_REVISION;
  mSmemStubProtocol.AllocFunc = SmemStubAllocate;
  mSmemStubProtocol.GetFunc   = SmemStubGetAddr;
  mSmemStubProtocol.Func2     = SmemStubFunc2;
  mSmemStubProtocol.Func3     = SmemStubFunc3;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiSMEMProtocolGuid,
                  &mSmemStubProtocol,
                  NULL
                  );

  DEBUG ((
    DEBUG_ERROR,
    "SmemStubDxe: EFI_SMEM_PROTOCOL install -> %r\n",
    Status
    ));

  return Status;
}
