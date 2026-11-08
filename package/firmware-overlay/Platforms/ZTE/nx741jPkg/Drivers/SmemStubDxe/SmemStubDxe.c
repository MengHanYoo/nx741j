/** @file
  SMEM stub driver for the nx741j UEFI bring-up.

  Publishes EFI_SMEM_PROTOCOL for the SMEM region described by the device tree
  (/soc/smem_region@81d00000, 2 MiB). The vendor SmemDxe expects the region to have been
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
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>

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

//
// Frame-buffer capture into the rawdump partition (31 MiB marker + 16 x 13 MiB slots).
// Lives here because SmemStubDxe is guaranteed to run; LogDumpDxe proved to be too early.
//
#define SS_BASE_BYTES   (4743976ULL * 4096ULL)
#define SS_LOG_BYTES    (SS_BASE_BYTES + 80ULL * 1024 * 1024)
#define SS_MARK_BYTES   (SS_LOG_BYTES - 8ULL * 4096)
#define SS_SLOT_BYTES   (3328ULL * 4096)
#define SS_MAX_DUMPS    16u

typedef struct {
  CHAR8   Magic[4];
  UINT32  Tick;
  UINT32  DiskFound;
  UINT32  GopFound;
  UINT32  DumpsDone;
  UINT32  BlockSize;
  UINT32  Reserved;
  UINT64  FrameBuffer;
  UINT64  FrameSize;
  UINT64  LastStatus;
} SS_MARKER;

STATIC EFI_BLOCK_IO_PROTOCOL         *mSsDisk;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mSsGop;
STATIC UINT8                         *mSsBuf;
STATIC UINTN                          mSsCount;
STATIC UINT32                         mSsTick;


//
// BDS never connects the UFS driver in this firmware, so no BlockIo handle exists.
// Connect every controller once so UFSDxe's binding start runs and disks appear.
//
STATIC
VOID
SsConnectAll (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Count = 0;
  EFI_HANDLE  *Handles = NULL;

  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || (Handles == NULL)) {
    DEBUG ((DEBUG_ERROR, "SmemStubDxe: no handles to connect (%r)\n", Status));
    return;
  }

  for (UINTN i = 0; i < Count; i++) {
    gBS->ConnectController (Handles[i], NULL, NULL, FALSE);
  }

  FreePool (Handles);
  DEBUG ((DEBUG_ERROR, "SmemStubDxe: connect all done (%u handles)\n", (UINT32)Count));
}

STATIC
VOID
SsFindDisk (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Count = 0;
  EFI_HANDLE  *Handles = NULL;
  UINT64      Best = 0;

  if (mSsDisk != NULL) {
    return;
  }

  //
  // Mu's LocateHandleBuffer returns zero handles for AllHandles/ByProtocol, so ask the
  // Boot Services for a protocol instance directly first; fall back to handle walking.
  //
  {
    EFI_BLOCK_IO_PROTOCOL  *Direct = NULL;

    Status = gBS->LocateProtocol (&gEfiBlockIoProtocolGuid, NULL, (VOID **)&Direct);
    if (!EFI_ERROR (Status) && (Direct != NULL) && (Direct->Media != NULL) &&
        (Direct->Media->BlockSize >= 512))
    {
      mSsDisk = Direct;
      mSsBuf  = AllocatePool ((UINTN)SS_SLOT_BYTES);
      DEBUG ((
        DEBUG_ERROR,
        "SmemStubDxe: direct disk=%p bs=0x%x last=0x%Lx buf=%p\n",
        mSsDisk,
        (UINT32)mSsDisk->Media->BlockSize,
        (UINT64)mSsDisk->Media->LastBlock,
        mSsBuf
        ));
      return;
    }
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SmemStubDxe: no blockio handles (%r)\n", Status));
    return;
  }

  for (UINTN i = 0; i < Count; i++) {
    EFI_BLOCK_IO_PROTOCOL  *Bio = NULL;
    UINT64                 Bytes;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio))) {
      continue;
    }

    if ((Bio == NULL) || (Bio->Media == NULL) || Bio->Media->LogicalPartition || (Bio->Media->BlockSize == 0)) {
      continue;
    }

    Bytes = (Bio->Media->LastBlock + 1) * (UINT64)Bio->Media->BlockSize;
    if (Bytes > Best) {
      Best     = Bytes;
      mSsDisk = Bio;
    }
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  if ((mSsDisk != NULL) && (mSsBuf == NULL)) {
    mSsBuf = AllocatePool ((UINTN)SS_SLOT_BYTES);
  }

  DEBUG ((
    DEBUG_ERROR,
    "SmemStubDxe: capture disk=%p bs=0x%x buf=%p\n",
    mSsDisk,
    (mSsDisk != NULL) ? (UINT32)mSsDisk->Media->BlockSize : 0,
    mSsBuf
    ));
}

STATIC
EFI_STATUS
SsWriteBytes (
  IN UINT64  Offset,
  IN VOID    *Buffer,
  IN UINTN   Size
  )
{
  UINTN  Bs;
  UINTN  Rounded;

  if ((mSsDisk == NULL) || (mSsDisk->Media == NULL) || (mSsBuf == NULL)) {
    return EFI_NOT_READY;
  }

  Bs = (UINTN)mSsDisk->Media->BlockSize;
  if (Bs == 0) {
    return EFI_NOT_READY;
  }

  Rounded = (Size + Bs - 1) & ~(Bs - 1);
  if (Rounded > SS_SLOT_BYTES) {
    Rounded = (UINTN)SS_SLOT_BYTES;
  }

  return mSsDisk->WriteBlocks (mSsDisk, mSsDisk->Media->MediaId, Offset / (UINT64)Bs, Rounded, Buffer);
}

STATIC
VOID
SsMarker (
  VOID
  )
{
  SS_MARKER  *m;

  if ((mSsDisk == NULL) || (mSsBuf == NULL)) {
    return;
  }

  ZeroMem (mSsBuf, 4096);
  m = (SS_MARKER *)mSsBuf;
  m->Magic[0] = 'S'; m->Magic[1] = 'S'; m->Magic[2] = 'D'; m->Magic[3] = 'X';
  m->Tick      = mSsTick++;
  m->DiskFound = (mSsDisk != NULL);
  m->GopFound  = (mSsGop != NULL);
  m->DumpsDone = (UINT32)mSsCount;
  m->BlockSize = (UINT32)mSsDisk->Media->BlockSize;
  if (mSsGop != NULL) {
    m->FrameBuffer = (UINT64)mSsGop->Mode->FrameBufferBase;
    m->FrameSize   = (UINT64)mSsGop->Mode->FrameBufferSize;
  }

  SsWriteBytes (SS_MARK_BYTES, mSsBuf, 4096);
}

STATIC
VOID
SsDump (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Size;

  SsFindDisk ();
  SsMarker ();

  if (mSsCount >= SS_MAX_DUMPS) {
    return;
  }

  if (mSsGop == NULL) {
    if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mSsGop)) || (mSsGop == NULL)) {
      return;
    }
  }

  if ((mSsDisk == NULL) || (mSsBuf == NULL)) {
    return;
  }

  Size = mSsGop->Mode->FrameBufferSize;
  if (Size == 0) {
    Size = (UINTN)mSsGop->Mode->Info->HorizontalResolution * mSsGop->Mode->Info->VerticalResolution * 4;
  }

  if (Size > SS_SLOT_BYTES) {
    Size = (UINTN)SS_SLOT_BYTES;
  }

  CopyMem (mSsBuf, (VOID *)(UINTN)mSsGop->Mode->FrameBufferBase, Size);
  Status = SsWriteBytes (SS_LOG_BYTES + (UINT64)mSsCount * SS_SLOT_BYTES, mSsBuf, Size);
  DEBUG ((DEBUG_ERROR, "SmemStubDxe: shot %u size 0x%x -> %r\n", (UINT32)mSsCount, (UINT32)Size, Status));
  mSsCount++;
}

STATIC
VOID
EFIAPI
SsTimer (
  EFI_EVENT  Event,
  VOID       *Context
  )
{
  SsDump ();
  if (mSsCount >= SS_MAX_DUMPS) {
    gBS->SetTimer (Event, TimerCancel, 0);
  }
}

STATIC
VOID
SsCaptureStart (
  VOID
  )
{
  EFI_EVENT  Event;

  DEBUG ((DEBUG_ERROR, "SmemStubDxe: capture start\n"));
  SsConnectAll ();
  SsFindDisk ();
  SsDump ();

  if (!EFI_ERROR (gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, SsTimer, NULL, &Event))) {
    gBS->SetTimer (Event, TimerPeriodic, 1000000);
  }
}

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

  // V6: capture disabled; the timer callback was crashing after BDS
  (VOID)SsCaptureStart;

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
