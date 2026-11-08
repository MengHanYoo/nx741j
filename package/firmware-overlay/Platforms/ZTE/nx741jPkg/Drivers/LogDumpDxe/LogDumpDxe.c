/** @file LogDumpDxe.c  (v2)

  Captures the graphics frame buffer (and a status marker) into the "rawdump" partition so
  the last screen of a crashing firmware can be recovered from a PC.

  Addresses are computed in BYTES from the start of the largest block device and divided by
  Media->BlockSize, so a driver reporting 512- or 4096-byte blocks both work.

    rawdump start (byte)  = 4743976 * 4096
    marker                = rawdump + 79 MiB   (one 4 KiB block)
    slot N (13 MiB)       = rawdump + 80 MiB + N * 13 MiB

**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>

#define RAWMDUMP_BYTES  (4743976ULL * 4096ULL)
#define LOG_BYTES       (RAWMDUMP_BYTES + 80ULL * 1024 * 1024)
#define MARKER_BYTES    (LOG_BYTES - 8ULL * 4096)
#define SLOT_BYTES      (3328ULL * 4096)          /* 13 MiB */
#define MAX_DUMPS       16u

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
} LOGDUMP_MARKER;

STATIC EFI_BLOCK_IO_PROTOCOL        *mDisk;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
STATIC UINT8                        *mBuf;
STATIC UINTN                         mBufSize;
STATIC UINTN                         mCount;
STATIC UINT32                        mTick;

STATIC
VOID
FindDisk (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Count = 0;
  EFI_HANDLE  *Handles = NULL;
  UINT64      Best = 0;

  if (mDisk != NULL) {
    return;
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return;
  }

  for (UINTN i = 0; i < Count; i++) {
    EFI_BLOCK_IO_PROTOCOL  *Bio = NULL;
    UINT64                 Bytes;

    if (EFI_ERROR (gBS->HandleProtocol (Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio))) {
      continue;
    }

    if ((Bio == NULL) || (Bio->Media == NULL) || Bio->Media->LogicalPartition) {
      continue;
    }

    if (Bio->Media->BlockSize == 0) {
      continue;
    }

    Bytes = (Bio->Media->LastBlock + 1) * (UINT64)Bio->Media->BlockSize;
    if (Bytes > Best) {
      Best  = Bytes;
      mDisk = Bio;
    }
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  if (mDisk != NULL) {
    mBufSize = (UINTN)SLOT_BYTES;
    mBuf     = AllocatePool (mBufSize);
    DEBUG ((
      DEBUG_ERROR,
      "LogDump: disk bs=0x%x blocks=0x%Lx buf=%p\n",
      (UINT32)mDisk->Media->BlockSize,
      mDisk->Media->LastBlock,
      mBuf
      ));
  }
}

STATIC
EFI_STATUS
WriteBytes (
  IN UINT64  Offset,
  IN VOID    *Buffer,
  IN UINTN   Size
  )
{
  UINTN  Bs;
  UINTN  Rounded;

  if ((mDisk == NULL) || (mDisk->Media == NULL)) {
    return EFI_NOT_READY;
  }

  Bs = (UINTN)mDisk->Media->BlockSize;
  if (Bs == 0) {
    return EFI_NOT_READY;
  }

  Rounded = (Size + Bs - 1) & ~(Bs - 1);
  if (Rounded > mBufSize) {
    Rounded = mBufSize;
  }

  return mDisk->WriteBlocks (
                  mDisk,
                  mDisk->Media->MediaId,
                  Offset / (UINT64)Bs,
                  Rounded,
                  Buffer
                  );
}

STATIC
VOID
WriteMarker (
  IN EFI_STATUS  Status
  )
{
  LOGDUMP_MARKER  *m;

  if ((mDisk == NULL) || (mBuf == NULL)) {
    return;
  }

  ZeroMem (mBuf, 4096);
  m = (LOGDUMP_MARKER *)mBuf;
  m->Magic[0] = 'L'; m->Magic[1] = 'D'; m->Magic[2] = 'X'; m->Magic[3] = '2';
  m->Tick       = mTick++;
  m->DiskFound  = (mDisk != NULL);
  m->GopFound   = (mGop != NULL);
  m->DumpsDone  = (UINT32)mCount;
  m->BlockSize  = (UINT32)mDisk->Media->BlockSize;
  m->LastStatus = (UINT64)Status;
  if (mGop != NULL) {
    m->FrameBuffer = (UINT64)mGop->Mode->FrameBufferBase;
    m->FrameSize   = (UINT64)mGop->Mode->FrameBufferSize;
  }

  WriteBytes (MARKER_BYTES, mBuf, 4096);
}

STATIC
VOID
Dump (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Size;

  FindDisk ();
  WriteMarker (EFI_SUCCESS);

  if (mCount >= MAX_DUMPS) {
    return;
  }

  if (mGop == NULL) {
    if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop)) || (mGop == NULL)) {
      return;
    }

    DEBUG ((
      DEBUG_ERROR,
      "LogDump: gop fb=0x%Lx size=0x%Lx\n",
      (UINT64)mGop->Mode->FrameBufferBase,
      (UINT64)mGop->Mode->FrameBufferSize
      ));
  }

  if ((mDisk == NULL) || (mBuf == NULL)) {
    return;
  }

  Size = mGop->Mode->FrameBufferSize;
  if (Size == 0) {
    Size = (UINTN)mGop->Mode->Info->HorizontalResolution * mGop->Mode->Info->VerticalResolution * 4;
  }

  if (Size > mBufSize) {
    Size = mBufSize;
  }

  CopyMem (mBuf, (VOID *)(UINTN)mGop->Mode->FrameBufferBase, Size);

  Status = WriteBytes (LOG_BYTES + (UINT64)mCount * SLOT_BYTES, mBuf, Size);
  DEBUG ((DEBUG_ERROR, "LogDump: slot %u size 0x%x -> %r\n", (UINT32)mCount, (UINT32)Size, Status));
  mCount++;
}

STATIC
VOID
EFIAPI
OnTimer (
  EFI_EVENT  Event,
  VOID       *Context
  )
{
  Dump ();
  if (mCount >= MAX_DUMPS) {
    gBS->SetTimer (Event, TimerCancel, 0);
  }
}

EFI_STATUS
EFIAPI
LogDumpDxeEntry (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  DEBUG ((DEBUG_ERROR, "LogDump: entry\n"));

  FindDisk ();
  if (mDisk != NULL) {
    WriteMarker (EFI_SUCCESS);
    Dump ();
  }

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnTimer, NULL, &Event);
  if (!EFI_ERROR (Status)) {
    gBS->SetTimer (Event, TimerPeriodic, 1000000);
  }

  return EFI_SUCCESS;
}
