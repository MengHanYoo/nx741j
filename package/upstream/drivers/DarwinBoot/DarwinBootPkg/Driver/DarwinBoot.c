// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Protocol/UniXNULoader.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/BlockIo.h>
STATIC VOID DbProbeWrite (IN CONST UNIXNU_LOADER_BOOT_REQUEST *Request);
STATIC VOID DbProbeFlush (VOID);
// Freeze on this mark so the screen stops scrolling and the last reached step
// can be photographed: bisect the handoff by changing this number.
#define HALT_MARK 0u

typedef struct { UINT32 Magic; UINT32 Step; UINT64 Status; UINT64 A; UINT64 B; UINT64 C; } DB_PROBE;
STATIC DB_PROBE  mDbProbe;
STATIC EFI_BLOCK_IO_PROTOCOL *mDbDisk;
STATIC UINT32    mMarkIndex;
#include "BootArgs.h"
#include "BootVideo.h"
#include "PackedRamdisk.h"
#include "WindowSize.h"

#define WINDOW_SIZE  SIZE_256MB
#define BLOCK_SIZE   SIZE_2MB
#define MAX_SEGMENTS 64

typedef struct {
  UINT64  Address;
  UINT64  Size;
  UINT64  FileOffset;
  UINT64  FileSize;
  UINT32  Protection;
} SEGMENT;

typedef struct {
  SEGMENT  Segments[MAX_SEGMENTS];
  UINTN    Count;
  UINT64   Base;
  UINT64   End;
  UINT64   Entry;
} KERNEL;

STATIC EFI_HANDLE  mImage;
UINT64 EFIAPI DarwinCurrentEl (VOID);
VOID EFIAPI DarwinEnter (UINT64 Args, UINT64 Entry, UINT64 Memory, UINT64 Size, UINT64 VideoBase, UINT64 VideoBytes);

#define READ_SYSREG(Name)  ({ UINT64 Value_; __asm__ volatile ("mrs %0, " #Name : "=r" (Value_)); Value_; })

STATIC UINT32 U32 (CONST VOID *P) { return ReadUnaligned32 ((CONST UINT32 *)P); }
STATIC UINT64 U64 (CONST VOID *P) { return ReadUnaligned64 ((CONST UINT64 *)P); }

STATIC
EFI_STATUS
ReadBootVideo (UINT64 Video[6])
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL           *Gop;
  EFI_STATUS                            Status;

  ZeroMem (Video, 6 * sizeof (*Video));
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  }
  if (Status == EFI_NOT_FOUND) { return EFI_SUCCESS; }
  if (EFI_ERROR (Status)) { return Status; }
  if ((Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) { return EFI_COMPROMISED_DATA; }
  // A GOP that only implements Blt has no linear framebuffer to hand over:
  // the kernel gets no video (Video stays zeroed) and the diagnostic marks
  // are drawn through Blt. Same outcome as a machine without a GOP.
  if ((Gop->Mode->FrameBufferBase == 0) || (Gop->Mode->FrameBufferSize == 0)) {
    Print (L"DARWINBOOT: GOP has no linear framebuffer; mark text goes through Blt, kernel video is zero\r\n");
    return EFI_SUCCESS;
  }
  Status = DarwinBootVideoFromGop (Gop, Video);
  // Say why a GOP was refused: EFI_UNSUPPORTED means the channel order and
  // EFI_COMPROMISED_DATA means the mode itself, and the big-type mark only
  // carries the status code.
  if (EFI_ERROR (Status) && (Gop->Mode != NULL) && (Gop->Mode->Info != NULL)) {
    Print (
      L"DARWINBOOT: video refused %r: format %u %ux%u stride %u fb %lx size %lx\r\n",
      Status,
      (UINT32)Gop->Mode->Info->PixelFormat,
      (UINT32)Gop->Mode->Info->HorizontalResolution,
      (UINT32)Gop->Mode->Info->VerticalResolution,
      (UINT32)Gop->Mode->Info->PixelsPerScanLine,
      (UINT64)Gop->Mode->FrameBufferBase,
      (UINT64)Gop->Mode->FrameBufferSize
      );
  }
  return Status;
}

STATIC
EFI_STATUS
ReadFile (
  EFI_FILE_PROTOCOL *Root,
  CONST CHAR16      *Path,
  UINTN             Limit,
  VOID              **Data,
  UINTN             *Size
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  UINTN              Actual;

  *Data = NULL;
  *Size = 0;
  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) { return Status; }
  Info = NULL;
  InfoSize = 0;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) { goto Done; }
  Info = AllocatePool (InfoSize);
  if (Info == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) { goto Done; }
  if ((Info->Attribute & EFI_FILE_DIRECTORY) || (Info->FileSize == 0) || (Info->FileSize > Limit)) {
    Print (L"DARWINBOOT: %s attr %lx size %lu limit %lu\n", Path, (UINT64)Info->Attribute, (UINT64)Info->FileSize, (UINT64)Limit);
    Status = EFI_BAD_BUFFER_SIZE;
    goto Done;
  }
  *Size = (UINTN)Info->FileSize;
  *Data = AllocatePool (*Size);
  if (*Data == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  Actual = *Size;
  Status = File->Read (File, &Actual, *Data);
  if (!EFI_ERROR (Status) && (Actual != *Size)) { Status = EFI_DEVICE_ERROR; }
Done:
  if (Info != NULL) { FreePool (Info); }
  File->Close (File);
  if (EFI_ERROR (Status) && (*Data != NULL)) { FreePool (*Data); *Data = NULL; }
  return Status;
}

STATIC
EFI_STATUS
ParseKernelHeader (CONST UINT8 *Data, UINTN Size, UINTN Header, KERNEL *Kernel)
{
  UINTN    Offset;
  UINTN    End;
  UINTN    Index;
  UINTN    Other;
  UINT32   Count;
  UINT32   Command;
  UINT32   Length;
  SEGMENT  *Segment;
  BOOLEAN  HasEntry;
  BOOLEAN  Executable;

  if ((Header > Size) || (Size - Header < 32) ||
      (U32 (Data + Header) != 0xfeedfacf) || (U32 (Data + Header + 4) != 0x100000c) ||
      (((U32 (Data + Header + 8) & 0x00ffffff) != 0) &&
       ((U32 (Data + Header + 8) & 0x00ffffff) != 2)) ||
      ((U32 (Data + Header + 12) != 2) &&
       ((Header != 0) || (U32 (Data + Header + 12) != 12)))) {
    return EFI_UNSUPPORTED;
  }
  if (U32 (Data + Header + 20) > Size - Header - 32) { return EFI_LOAD_ERROR; }
  ZeroMem (Kernel, sizeof (*Kernel));
  Count = U32 (Data + Header + 16);
  End = Header + 32 + U32 (Data + Header + 20);
  Offset = Header + 32;
  HasEntry = FALSE;
  for (Index = 0; Index < Count; Index++) {
    if (End - Offset < 8) { return EFI_LOAD_ERROR; }
    Command = U32 (Data + Offset);
    Length = U32 (Data + Offset + 4);
    if ((Length < 8) || (Length & 7) || (Length > End - Offset)) { return EFI_LOAD_ERROR; }
    if (Command == 0x19) {
      if ((Length < 72) || (U32 (Data + Offset + 64) > (Length - 72) / 80) ||
          (Kernel->Count == MAX_SEGMENTS)) { return EFI_LOAD_ERROR; }
      if (U64 (Data + Offset + 32) != 0) {
        Segment = &Kernel->Segments[Kernel->Count++];
        Segment->Address = U64 (Data + Offset + 24);
        Segment->Size = U64 (Data + Offset + 32);
        Segment->FileOffset = U64 (Data + Offset + 40);
        Segment->FileSize = U64 (Data + Offset + 48);
        Segment->Protection = U32 (Data + Offset + 60);
        if ((Segment->Size > WINDOW_SIZE) || (Segment->Address > MAX_UINT64 - Segment->Size) ||
            (Segment->FileSize > Segment->Size) || (Segment->FileOffset > Size) ||
            (Segment->FileSize > Size - Segment->FileOffset)) { return EFI_LOAD_ERROR; }
      }
    } else if (Command == 5) {
      if (HasEntry || (Length != 288) || (U32 (Data + Offset + 8) != 6) ||
          (U32 (Data + Offset + 12) != 68)) { return EFI_LOAD_ERROR; }
      Kernel->Entry = U64 (Data + Offset + 272);
      HasEntry = TRUE;
    }
    Offset += Length;
  }
  if ((Offset != End) || !HasEntry || (Kernel->Count == 0)) { return EFI_LOAD_ERROR; }
  Kernel->Base = MAX_UINT64;
  Executable = FALSE;
  for (Index = 0; Index < Kernel->Count; Index++) {
    Segment = &Kernel->Segments[Index];
    Kernel->Base = MIN (Kernel->Base, Segment->Address);
    Kernel->End = MAX (Kernel->End, Segment->Address + Segment->Size);
    if ((Kernel->Entry >= Segment->Address) && (Kernel->Entry - Segment->Address < Segment->FileSize) &&
        ((Segment->Protection & 4) != 0)) { Executable = TRUE; }
    for (Other = 0; Other < Index; Other++) {
      if ((Segment->Address < Kernel->Segments[Other].Address + Kernel->Segments[Other].Size) &&
          (Kernel->Segments[Other].Address < Segment->Address + Segment->Size)) { return EFI_LOAD_ERROR; }
    }
  }
  if (!Executable || (Kernel->Entry & 3) || (Kernel->Base & EFI_PAGE_MASK) ||
      (Kernel->Base > MAX_UINT64 - WINDOW_SIZE) ||
      (Kernel->End - Kernel->Base > WINDOW_SIZE - SIZE_2MB)) { return EFI_LOAD_ERROR; }
  return EFI_SUCCESS;
}

// Fileset segment offsets are absolute in the collection. XNU rebases and
// signs its own fixup chains after entry; firmware only copies root mappings.
STATIC
EFI_STATUS
ParseKernel (CONST UINT8 *Data, UINTN Size, KERNEL *Kernel)
{
  EFI_STATUS Status;
  KERNEL     Member;
  UINTN      Offset;
  UINTN      End;
  UINTN      Header;
  UINTN      Name;
  UINTN      Index;
  UINTN      Other;
  UINT32     Length;
  BOOLEAN    Found;
  BOOLEAN    Covered;
  SEGMENT    *Child;
  SEGMENT    *Parent;

  Status = ParseKernelHeader (Data, Size, 0, Kernel);
  if (EFI_ERROR (Status) || (U32 (Data + 12) != 12)) { return Status; }
  Offset = 32;
  End = 32 + U32 (Data + 20);
  Found = FALSE;
  while (Offset < End) {
    Length = U32 (Data + Offset + 4);
    if (U32 (Data + Offset) == 0x80000035) {
      if (Length < 40) { return EFI_LOAD_ERROR; }
      Name = U32 (Data + Offset + 24);
      if ((Name < 32) || (Name >= Length)) { return EFI_LOAD_ERROR; }
      for (Index = Name; (Index < Length) && (Data[Offset + Index] != 0); Index++) {}
      if (Index == Length) { return EFI_LOAD_ERROR; }
      Header = U64 (Data + Offset + 16);
      if ((Header > Size) || (Size - Header < 32) || (Header & 7)) { return EFI_LOAD_ERROR; }
      // Every indexed header must actually be loaded at its advertised VA.
      Covered = FALSE;
      for (Other = 0; Other < Kernel->Count; Other++) {
        Parent = &Kernel->Segments[Other];
        if ((Header >= Parent->FileOffset) &&
            (Header - Parent->FileOffset <= Parent->FileSize) &&
            (32 <= Parent->FileSize - (Header - Parent->FileOffset)) &&
            (U64 (Data + Offset + 8) == Parent->Address + Header - Parent->FileOffset)) {
          Covered = TRUE;
        }
      }
      if (!Covered) { return EFI_LOAD_ERROR; }
      if ((Index - Name == sizeof ("com.apple.kernel") - 1) &&
          (CompareMem (Data + Offset + Name, "com.apple.kernel", sizeof ("com.apple.kernel")) == 0)) {
        if (Found || (U32 (Data + Header + 12) != 2) ||
            (U32 (Data + Header + 8) != U32 (Data + 8)) ||
            ((U32 (Data + Header + 24) & 0x80000000) == 0)) { return EFI_LOAD_ERROR; }
        Found = TRUE;
        Status = ParseKernelHeader (Data, Size, Header, &Member);
        if (EFI_ERROR (Status) || (Member.Entry != Kernel->Entry)) { return EFI_LOAD_ERROR; }
        for (Index = 0; Index < Member.Count; Index++) {
          Child = &Member.Segments[Index];
          Covered = FALSE;
          for (Other = 0; Other < Kernel->Count; Other++) {
            Parent = &Kernel->Segments[Other];
            if ((Child->Address >= Parent->Address) &&
                (Child->Address - Parent->Address <= Parent->Size) &&
                (Child->Size <= Parent->Size - (Child->Address - Parent->Address)) &&
                ((Child->Protection & ~Parent->Protection) == 0) &&
                ((Child->FileSize == 0) ||
                 ((Child->Address - Parent->Address <= Parent->FileSize) &&
                  (Child->FileSize <= Parent->FileSize - (Child->Address - Parent->Address)) &&
                  (Child->FileOffset == Parent->FileOffset + Child->Address - Parent->Address)))) {
              Covered = TRUE;
            }
          }
          if (!Covered) { return EFI_LOAD_ERROR; }
        }
      }
    }
    Offset += Length;
  }
  return Found ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

STATIC
BOOLEAN
AdtNode (CONST UINT8 *Data, UINTN Size, UINTN *Offset, UINTN Depth, UINTN ParentPath, UINTN *RamdiskOffset, UINTN *TrustCacheOffset, UINTN *AddonOffset, UINTN *PolicyOffset, UINT64 *WindowSize, BOOLEAN *KeepPacked)
{
  UINT32 Properties;
  UINT32 Children;
  UINT32 Length;
  UINTN  Index;
  UINTN  Path;
  UINTN  Candidate;
  UINTN  TrustCandidate;
  UINTN  AddonCandidate;
  UINTN  PolicyCandidate;
  UINTN  MemoryCandidate;
  CONST UINT8 *Name;
  UINTN  NameLength;

  Name = NULL;
  NameLength = 0;
  Candidate = MAX_UINTN;
  TrustCandidate = MAX_UINTN;
  AddonCandidate = MAX_UINTN;
  PolicyCandidate = MAX_UINTN;
  MemoryCandidate = MAX_UINTN;
  if ((Depth > 64) || (Size - *Offset < 8)) { return FALSE; }
  Properties = U32 (Data + *Offset);
  Children = U32 (Data + *Offset + 4);
  *Offset += 8;
  if (Properties > (Size - *Offset) / 36) { return FALSE; }
  for (Index = 0; Index < Properties; Index++) {
    if (Size - *Offset < 36) { return FALSE; }
    if (ScanMem8 (Data + *Offset, 32, 0) == NULL) { return FALSE; }
    Length = U32 (Data + *Offset + 32);
    *Offset += 36;
    if (Length > Size - *Offset) { return FALSE; }
    if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "name") == 0) {
      if (Name != NULL) { return FALSE; }
      Name = Data + *Offset;
      NameLength = Length;
    }
    if ((AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "RAMDisk") == 0) ||
        (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "PackedRAMDisk") == 0)) {
      if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "PackedRAMDisk") == 0) {
        if (*KeepPacked) { return FALSE; }
        *KeepPacked = TRUE;
      }
      if ((Candidate != MAX_UINTN) || (Length != 16)) { return FALSE; }
      Candidate = *Offset;
    }
    if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "TrustCache") == 0) {
      if ((TrustCandidate != MAX_UINTN) || (Length != 16)) { return FALSE; }
      TrustCandidate = *Offset;
    }
    if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "GpuAddon") == 0) {
      if ((AddonCandidate != MAX_UINTN) || (Length != 16)) { return FALSE; }
      AddonCandidate = *Offset;
    }
    if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "RootPolicies") == 0) {
      if ((PolicyCandidate != MAX_UINTN) || (Length != 16)) { return FALSE; }
      PolicyCandidate = *Offset;
    }
    if (AsciiStrCmp ((CONST CHAR8 *)(Data + *Offset - 36), "darwinboot-memory-size") == 0) {
      if ((MemoryCandidate != MAX_UINTN) || (Length != 8)) { return FALSE; }
      MemoryCandidate = *Offset;
    }
    *Offset += Length;
    if (ALIGN_VALUE (*Offset, 4) > Size) { return FALSE; }
    *Offset = ALIGN_VALUE (*Offset, 4);
  }
  Path = 3;
  if (Depth == 0) { Path = 0; }
  if ((Depth == 1) && (ParentPath == 0) && (NameLength == 7) && (CompareMem (Name, "chosen", 7) == 0)) { Path = 1; }
  if ((Depth == 2) && (ParentPath == 1) && (NameLength == 11) && (CompareMem (Name, "memory-map", 11) == 0)) { Path = 2; }
  if ((Path == 2) && (Candidate != MAX_UINTN)) {
    if (*RamdiskOffset != MAX_UINTN) { return FALSE; }
    *RamdiskOffset = Candidate;
  }
  if ((Path == 2) && (TrustCandidate != MAX_UINTN)) {
    if (*TrustCacheOffset != MAX_UINTN) { return FALSE; }
    *TrustCacheOffset = TrustCandidate;
  }
  if ((Path == 2) && (AddonCandidate != MAX_UINTN)) {
    if (*AddonOffset != MAX_UINTN) { return FALSE; }
    *AddonOffset = AddonCandidate;
  }
  if (PolicyCandidate != MAX_UINTN) {
    if ((Path != 2) || (*PolicyOffset != MAX_UINTN)) { return FALSE; }
    *PolicyOffset = PolicyCandidate;
  }
  if (MemoryCandidate != MAX_UINTN) {
    if ((Path != 1) || (*WindowSize != 0)) { return FALSE; }
    *WindowSize = U64 (Data + MemoryCandidate);
    if (!DarwinWindowSizeValid (*WindowSize)) { return FALSE; }
  }
  if (Children > (Size - *Offset) / 8) { return FALSE; }
  for (Index = 0; Index < Children; Index++) {
    if (!AdtNode (Data, Size, Offset, Depth + 1, Path, RamdiskOffset, TrustCacheOffset, AddonOffset, PolicyOffset, WindowSize, KeepPacked)) { return FALSE; }
  }
  return TRUE;
}

STATIC
BOOLEAN
ValidTrustCaches (CONST UINT8 *Data, UINTN Size)
{
  UINTN Count;
  UINTN Index;
  UINTN Start;
  UINTN End;
  UINTN Entries;
  UINTN Entry;

  if (Size < 8) { return FALSE; }
  Count = U32 (Data);
  if ((Count == 0) || (Count > 128) || (Count > (Size - 4) / 4)) { return FALSE; }
  for (Index = 0; Index < Count; Index++) {
    Start = U32 (Data + 4 + Index * 4);
    End = (Index + 1 < Count) ? U32 (Data + 8 + Index * 4) : Size;
    if ((Start < 4 + Count * 4) || (Start & 3) || (End > Size) ||
        (End < Start) || (End - Start < 24) || (U32 (Data + Start) != 2)) { return FALSE; }
    Entries = U32 (Data + Start + 20);
    if ((Entries > (End - Start - 24) / 24) || (End - Start != 24 + Entries * 24)) { return FALSE; }
    for (Entry = 0; Entry < Entries; Entry++) {
      CONST UINT8 *Hash = Data + Start + 24 + Entry * 24;
      if ((Hash[20] < 1) || (Hash[20] > 4) ||
          ((Entry != 0) && (CompareMem (Hash - 24, Hash, 20) >= 0))) { return FALSE; }
    }
  }
  return TRUE;
}

STATIC
EFI_STATUS
GetMap (EFI_MEMORY_DESCRIPTOR **Map, UINTN *Capacity, UINTN *Size, UINTN *Key, UINTN *Stride)
{
  EFI_STATUS Status;
  UINT32     Version;
  UINTN      Attempt;

  for (Attempt = 0; Attempt < 3; Attempt++) {
    *Size = *Capacity;
    Status = gBS->GetMemoryMap (Size, *Map, Key, Stride, &Version);
    if (Status != EFI_BUFFER_TOO_SMALL) { return Status; }
    if (*Map != NULL) { FreePool (*Map); }
    *Capacity = *Size + 32 * sizeof (EFI_MEMORY_DESCRIPTOR);
    *Map = AllocatePool (*Capacity);
    if (*Map == NULL) { return EFI_OUT_OF_RESOURCES; }
  }
  return EFI_OUT_OF_RESOURCES;
}

// End of the run of EfiConventionalMemory that begins at Start: the UEFI map often lists physically
// adjacent free ranges as separate descriptors, so extend over every descriptor that continues the run.
STATIC
UINT64
RunEnd (EFI_MEMORY_DESCRIPTOR *Map, UINTN Size, UINTN Stride, UINT64 Start, UINT64 End)
{
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  UINTN                 Offset;
  BOOLEAN               Extended;

  do {
    Extended = FALSE;
    for (Offset = 0; Offset + Stride <= Size; Offset += Stride) {
      Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
      if ((Descriptor->Type == EfiConventionalMemory) && (Descriptor->PhysicalStart == End) &&
          (Descriptor->NumberOfPages != 0) && (Descriptor->NumberOfPages < (MAX_UINT64 - End) / EFI_PAGE_SIZE)) {
        End += EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages);
        Extended = TRUE;
      }
    }
  } while (Extended);
  (VOID)Start;
  return End;
}

// Large diagnostic text straight into the GOP framebuffer: the UEFI text console font is too small to
// photograph. 5x12 glyphs from DiagFont.h scaled 4x, white on a black box; wraps at the right edge.
#include "DiagFont.h"
STATIC CONST UINT64 mDiagFont[] = GLYPH_FONT;

STATIC UINT32 *mFbBase;
STATIC UINT64  mFbWidth, mFbHeight, mFbStride;
STATIC BOOLEAN mFbValid;
STATIC BOOLEAN mBsGone;
// GOP without a linear framebuffer (QEMU virtio-gpu under AAVMF reports
// PixelBitMask and Base/Size 0): the marks go through Blt instead.
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mBltGop;
STATIC BOOLEAN mProbeWrite;
STATIC BOOLEAN mScratchVideo;
STATIC BOOLEAN mNoVideo;
STATIC UINT64  mVideoBarBase;   // address the firmware can write for the green bar
#define SCRATCH_ROW     2560u
#define SCRATCH_HEIGHT  480u

// QEMU-only witness: the PL011 UART at 0x09000000 is not boot-services based,
// so it keeps reporting after ExitBootServices. Enabled by "qemu-mark=1" only.
STATIC
VOID
QemuMark (CONST CHAR8 *Text)
{
  volatile UINT32      *uart = (volatile UINT32 *)(UINTN)0x09000000;

  while (*Text != 0) {
    *uart = (UINT8)*Text++;
  }
}

// Blt-only GOPs (QEMU virtio-gpu under AAVMF reports PixelBitMask and no
// framebuffer) cannot be written through a pointer: render the line into a
// buffer and hand it to Blt, the same call UniXNU's menu uses.
STATIC
VOID
DiagDrawBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
  IN CONST CHAR16 *Text,
  IN UINTN Row
  )
{
  UINTN  Scale = 4;
  UINTN  Cell  = 6 * Scale;
  UINTN  Line  = 14 * Scale;
  UINTN  Width;
  UINTN  Height;
  UINTN  Chars;
  UINTN  Cols;
  UINTN  Rows;
  UINTN  Used;
  UINTN  Col;
  UINTN  X;
  UINTN  Y;
  UINTN  Index;
  UINTN  Gx;
  UINTN  Gy;
  UINTN  Dx;
  UINTN  Dy;
  UINT64 Glyph;
  UINT64 Data;
  UINT16 Ch;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buf;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *P;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black;

  if ((Gop == NULL) || (Gop->Blt == NULL) || (Gop->Mode == NULL) || (Gop->Mode->Info == NULL)) { return; }
  Width  = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;
  if ((Width < Cell) || (Height < Line)) { return; }
  for (Chars = 0; (Text[Chars] != 0) && (Chars < 128); Chars++) { }
  if (Chars == 0) { return; }
  Cols = Width / Cell;
  Rows = (Chars + Cols - 1) / Cols;
  Used = (Chars < Cols) ? Chars : Cols;
  Y    = Height / 4 + Row * Line;
  if (Y + Rows * Line > Height) { Y = (Rows * Line < Height) ? Height - Rows * Line : 0; }
  Buf = AllocatePool (Used * Cell * Rows * Line * sizeof (*Buf));
  if (Buf == NULL) { return; }
  for (Index = 0; Index < Used * Cell * Rows * Line; Index++) {
    Buf[Index].Blue = 0; Buf[Index].Green = 0; Buf[Index].Red = 0; Buf[Index].Reserved = 0xFF;
  }
  for (Index = 0; Index < Chars; Index++) {
    Ch = Text[Index];
    if ((Ch < 32) || (Ch > 126)) { continue; }
    if ((Index / Cols) >= Rows) { break; }
    Col = Index % Cols;
    if (Col >= Used) { continue; }
    X     = Col * Cell;
    Glyph = mDiagFont[Ch - 32];
    for (Gy = 0; Gy < 12; Gy++) {
      Data = (Gy < 6) ? ((Glyph >> 32) >> (Gy * 5)) : ((Glyph & 0xFFFFFFFFULL) >> ((Gy - 6) * 5));
      for (Gx = 0; Gx < 5; Gx++) {
        if (((Data >> Gx) & 1) == 0) { continue; }
        for (Dy = 0; Dy < Scale; Dy++) {
          for (Dx = 0; Dx < Scale; Dx++) {
            P = &Buf[((Index / Cols) * Line + Gy * Scale + Dy) * (Used * Cell) + X + Gx * Scale + Dx];
            P->Blue = 0xFF; P->Green = 0xFF; P->Red = 0xFF; P->Reserved = 0xFF;
          }
        }
      }
    }
  }
  // Clear the whole row first: a mark is usually shorter than the previous one
  // and its tail would stay visible next to the new text.
  Black.Blue = 0; Black.Green = 0; Black.Red = 0; Black.Reserved = 0xFF;
  Gop->Blt (Gop, &Black, EfiBltVideoFill, 0, 0, 0, Y, Width, Rows * Line, 0);
  Gop->Blt (Gop, Buf, EfiBltBufferToVideo, 0, 0, 0, Y, Used * Cell, Rows * Line, 0);
  FreePool (Buf);
}
STATIC
VOID
DiagDraw (CONST CHAR16 *Text, UINTN Row)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
  UINT64                       Video[6];
  UINTN                        Scale = 4, X = 0, Y, Index, Gx, Gy, Dx, Dy, Cell, Line;
  UINT32                       *Fb;
  UINT64                       Glyph, Data;
  CHAR16                       Ch;

  // Cache the framebuffer once: after ExitBootServices the protocol is gone but
  // the pixels are still writable, and the last line has to stay readable.
  if (!mFbValid && (mBltGop == NULL)) {
    // After ExitBootServices gBS is dead: never dereference it again, or the
    // machine takes a synchronous exception at 0x0 (seen in QEMU without GOP).
    if (mBsGone) { return; }
    if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop))) { return; }
    if (!EFI_ERROR (DarwinBootVideoFromGop (Gop, Video)) && (Video[0] != 0)) {
      mFbBase   = (UINT32 *)(UINTN)Video[0];
      mFbWidth  = Video[3];
      mFbHeight = Video[4];
      mFbStride = Video[2] / 4;
      mFbValid  = TRUE;
    } else if ((Gop->Blt != NULL) && (Gop->Mode != NULL) && (Gop->Mode->Info != NULL)) {
      mBltGop = Gop;
    } else {
      return;
    }
  }
  if (mBltGop != NULL) {
    // Blt is a boot-service call: the cached protocol must not be used after
    // ExitBootServices, unlike the plain framebuffer.
    if (mBsGone) { return; }
    DiagDrawBlt (mBltGop, Text, Row);
    return;
  }
  Fb = mFbBase;
  Video[0] = (UINT64)(UINTN)mFbBase;
  Video[2] = (UINT64)(mFbStride * 4);
  Video[3] = mFbWidth;
  Video[4] = mFbHeight;
  if (!Fb) { return; }
  Cell = 6 * Scale;
  Line = 14 * Scale;
  Y = Video[4] / 4 + Row * Line;
  Fb = (UINT32 *)(UINTN)Video[0];
  for (Index = 0; Text[Index] != 0; Index++) {
    Ch = Text[Index];
    if ((Ch < 32) || (Ch > 126)) { continue; }
    if (X + Cell > Video[3]) { X = 0; Y += Line; }
    if (Y + Line > Video[4]) { return; }
    Glyph = mDiagFont[Ch - 32];
    for (Gy = 0; Gy < 14 * Scale; Gy++) {
      for (Gx = 0; Gx < Cell; Gx++) {
        Fb[(Y + Gy) * (Video[2] / 4) + X + Gx] = 0xFF000000;
      }
    }
    for (Gy = 0; Gy < 12; Gy++) {
      Data = (Gy < 6) ? ((Glyph >> 32) >> (Gy * 5)) : ((Glyph & 0xFFFFFFFFULL) >> ((Gy - 6) * 5));
      for (Gx = 0; Gx < 5; Gx++) {
        if ((Data >> Gx) & 1) {
          for (Dy = 0; Dy < Scale; Dy++) {
            for (Dx = 0; Dx < Scale; Dx++) {
              Fb[(Y + Gy * Scale + Dy) * (Video[2] / 4) + X + Gx * Scale + Dx] = 0xFFFFFFFF;
            }
          }
        }
      }
    }
    X += Cell;
  }
}


// Every step mark goes to the GOP in big letters AND to ConOut: DiagDraw can
// return silently when the video protocol validation fails, which would look
// exactly like "the code stopped".
STATIC
VOID
Mark (
  CONST CHAR16 *Text,
  UINTN        Row
  )
{
  ++mMarkIndex;
  // The block write is a hardware side effect on every step and is only needed
  // when a session asks for it (probe-write=1); the screen and QEMU serial are
  // the normal channels.
  if (mProbeWrite) {
    mDbProbe.Step = mMarkIndex;
    mDbProbe.Status = 0;
    DbProbeFlush ();
  }
  Print (L"MARK #%u %s\r\n", mMarkIndex, Text);
  DiagDraw (Text, Row);
  if (mMarkIndex == HALT_MARK) {
    for (;;) {
      DiagDraw (Text, Row);
      gBS->Stall (200000);
    }
  }
}

STATIC
EFI_STATUS
AllocateWindow (KERNEL *Kernel, UINT64 WindowSize, EFI_PHYSICAL_ADDRESS *Address)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  EFI_STATUS            Status;
  UINTN                 Capacity;
  UINTN                 Size;
  UINTN                 Key;
  UINTN                 Stride;
  UINTN                 Offset;
  UINT64                End;
  UINT64                Candidate;
  UINT64                BestStart;
  UINT64                BestBytes;
  UINT64                RunBytes;

  *Address = 0;
  Map = NULL;
  Capacity = 0;
  BestStart = 0;
  BestBytes = 0;
  Status = GetMap (&Map, &Capacity, &Size, &Key, &Stride);
  if (EFI_ERROR (Status)) { if (Map != NULL) { FreePool (Map); } return Status; }
  Status = EFI_OUT_OF_RESOURCES;
  Status = EFI_OUT_OF_RESOURCES;
  for (Offset = 0; Offset + Stride <= Size; Offset += Stride) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    if ((Descriptor->Type != EfiConventionalMemory) || (Descriptor->NumberOfPages == 0)) { continue; }
    End = RunEnd (Map, Size, Stride, Descriptor->PhysicalStart,
                  Descriptor->PhysicalStart + EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages));
    RunBytes = End - Descriptor->PhysicalStart;
    if (RunBytes > BestBytes) { BestBytes = RunBytes; BestStart = Descriptor->PhysicalStart; }
    Candidate = ALIGN_VALUE (Descriptor->PhysicalStart, BLOCK_SIZE) + (Kernel->Base & (BLOCK_SIZE - 1));
    if ((Candidate < Descriptor->PhysicalStart) || (End < Candidate) || (End - Candidate < WindowSize)) { continue; }
    Status = gBS->AllocatePages (AllocateAddress, EfiLoaderCode, EFI_SIZE_TO_PAGES (WindowSize), &Candidate);
    if (!EFI_ERROR (Status)) { *Address = Candidate; break; }
  }
  if (EFI_ERROR (Status)) {
    CHAR16 Line[96];
    UINTN  Row = 1;
    UnicodeSPrint (Line, sizeof (Line), L"NO WINDOW %lx BIG RUN %lx @ %lx", WindowSize, BestBytes, BestStart);
    DiagDraw (Line, 0);
    // What bounds the biggest run from above: its type tells whether the 64 MiB
    // payload buffer (EfiLoaderData) or something else sits next to it.
    for (Offset = 0; Offset + Stride <= Size; Offset += Stride) {
      Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
      if (Descriptor->PhysicalStart == BestStart + BestBytes) {
        UnicodeSPrint (Line, sizeof (Line), L"NEXT t%lx @ %lx + %lx", (UINT64)Descriptor->Type,
                       (UINT64)Descriptor->PhysicalStart,
                       (UINT64)EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages));
        DiagDraw (Line, Row++);
        break;
      }
    }
    for (Offset = 0; Offset + Stride <= Size; Offset += Stride) {
      Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
      if ((Descriptor->Type == EfiConventionalMemory) && (Descriptor->NumberOfPages >= 0x400) && (Row < 8)) {
        UnicodeSPrint (Line, sizeof (Line), L"FREE %lx + %lx", Descriptor->PhysicalStart, EFI_PAGES_TO_SIZE (Descriptor->NumberOfPages));
        DiagDraw (Line, Row++);
      }
    }
    // Keep the diagnostic on screen: the generic Fail mark would overwrite it.
    for (;;) {
      gBS->Stall (200000);
    }
  }
  FreePool (Map);
  return Status;
}


// ---- persistent boot probe (rawdump + 79 MiB) ------------------------------
#define DB_PROBE_OFFSET (4743976ULL * 4096ULL + 79ULL * 1024 * 1024)
// Persist the current mark: the screen scrolls too fast to read on hardware,
// but this sector can be read back over 9008 afterwards.
STATIC
VOID
DbProbeFlush (
  VOID
  )
{
  if (mDbDisk == NULL || mDbDisk->Media == NULL || mDbDisk->Media->BlockSize == 0) {
    return;
  }
  mDbProbe.Magic = 0x32444244;
  mDbDisk->WriteBlocks (mDbDisk, mDbDisk->Media->MediaId,
                        DB_PROBE_OFFSET / mDbDisk->Media->BlockSize, 4096, &mDbProbe);
}

STATIC
VOID
DbProbeWrite (
  IN CONST UNIXNU_LOADER_BOOT_REQUEST *Request
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Dp = NULL;
  EFI_HANDLE                Disk = NULL;
  EFI_BLOCK_IO_PROTOCOL     *Bio = NULL;

  mDbProbe.Magic = 0x32444244;
  if (Request != NULL && Request->Volume != NULL) {
    if (!EFI_ERROR (gBS->HandleProtocol (Request->Volume, &gEfiDevicePathProtocolGuid, (VOID **)&Dp)) && Dp != NULL) {
      gBS->LocateDevicePath (&gEfiBlockIoProtocolGuid, &Dp, &Disk);
      gBS->HandleProtocol (Disk, &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
    }
  }
  if (Bio == NULL || Bio->Media == NULL || Bio->Media->BlockSize == 0) {
    return;
  }
  mDbDisk = Bio;
  mDbDisk->WriteBlocks (mDbDisk, mDbDisk->Media->MediaId,
                        DB_PROBE_OFFSET / mDbDisk->Media->BlockSize, 4096, &mDbProbe);
}
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
EFIAPI
Boot (
  IN UNIXNU_LOADER_PROTOCOL    *This,
  IN CONST UNIXNU_LOADER_BOOT_REQUEST *Request
  )
{
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL               *Root;
  VOID                            *Image;
  VOID                            *Tree;
  VOID                            *TrustCaches;
  VOID                            *Addon;
  UINTN                           ImageSize;
  UINTN                           TreeSize;
  UINTN                           TrustCacheSize;
  UINTN                           AddonSize;
  CHAR16                          *TreePath;
  UINTN                           Length;
  UINTN                           Index;
  UINTN                           Offset;
  KERNEL                          Kernel;
  EFI_PHYSICAL_ADDRESS            Address;
  UINT64                          ArgsAddress;
  UINT64                          TreeAddress;
  UINT64                          Used;
  UINT64                          WindowSize;
  UINT64                          RamdiskAddress;
  UINTN                           RamdiskOffset;
  UINTN                           RamdiskBytes;
  UINTN                           RamdiskExpandedSize;
  BOOLEAN                         RamdiskPacked;
  BOOLEAN                         KeepPacked;
  UINTN                           TrustCacheOffset;
  UINT64                          TrustCacheAddress;
  UINTN                           AddonOffset;
  UINT64                          AddonAddress;
  VOID                            *Policies;
  UINTN                           PolicySize;
  UINTN                           PolicyOffset;
  UINT64                          PolicyAddress;
  DARWIN_BOOT_ARGS                *Args;
  EFI_MEMORY_DESCRIPTOR           *Map;
  UINTN                           Capacity;
  UINTN                           MapSize;
  UINTN                           Key;
  UINTN                           Stride;
  UINT32                          Version;
  UINTN                           Attempt;
  CHAR8                           CommandLine[1024];
  UINT64                          Video[6];

  (VOID)This;
  mDbProbe.Step = 0; DbProbeWrite (Request);
  Mark (L"[0] entry", 0);
  DEBUG ((DEBUG_ERROR, "DARWINBOOT: Boot called, EL%u req %lx fdt %lx/%lu ramdisk %lu\n",
         (UINT32)(DarwinCurrentEl () >> 2), (UINT64)(UINTN)Request,
         Request != NULL ? (UINT64)(UINTN)Request->DeviceTree : 0ULL,
         Request != NULL ? (UINT64)Request->DeviceTreeSize : 0ULL,
         Request != NULL ? (UINT64)Request->RamdiskSize : 0ULL));
  // Stall removed for bring-up: timer events are suspects here.
  Mark (L"[0a] stall", 0);
  mDbProbe.Step = 1; DbProbeWrite (Request);
  Mark (L"[0b] probe1", 0);
  if ((Request == NULL) || (Request->Volume == NULL) || (Request->KernelPath == NULL)) { return EFI_INVALID_PARAMETER; }
  mDbProbe.Step = 2; DbProbeWrite (Request);
  Mark (L"[0c] probe2", 0);
  if ((Request->KextCount != 0) || (DarwinCurrentEl () != 4)) {
    Print (L"DARWINBOOT: unsupported inputs or exception level; firmware retained\n");
    return EFI_UNSUPPORTED;
  }
  //
  // UniXNU may pass the device tree it prepared (boot-chain step 3). Accept and validate
  // it, but keep boot_args.DeviceTree on the ADT sidecar: the platform expert matches the
  // ADT root, not the hardware tree.
  //
  if ((Request->DeviceTree != NULL) || (Request->DeviceTreeSize != 0)) {
    if ((Request->DeviceTree == NULL) || (Request->DeviceTreeSize == 0)) {
      Print (L"DARWINBOOT: request tree pointer/size disagree; firmware retained\n");
      return EFI_INVALID_PARAMETER;
    }
    if ((Request->DeviceTreeSize >= 8) &&
        (SwapBytes32 (*(UINT32 *)Request->DeviceTree) != 0xd00dfeedu)) {
      Print (L"DARWINBOOT: request tree is not a flattened device tree\n");
      return EFI_VOLUME_CORRUPTED;
    }
    Print (L"DARWINBOOT: request tree %lx/%lu received; boot_args tree stays the ADT sidecar\n",
           (UINT64)(UINTN)Request->DeviceTree, (UINT64)Request->DeviceTreeSize);
  }
  Mark (L"[0d] ramlayout", 0);
  if (!RamdiskLayout (Request->Ramdisk, Request->RamdiskSize, &RamdiskExpandedSize, &RamdiskPacked)) {
    Print (L"DARWINBOOT: ramdisk layout rejected, %lu bytes\n", (UINT64)Request->RamdiskSize);
    return EFI_BAD_BUFFER_SIZE;
  }
  ZeroMem (CommandLine, sizeof (CommandLine));
  if (Request->BootArgs != NULL) {
    for (Index = 0; Request->BootArgs[Index] != 0; Index++) {
      if ((Index >= sizeof (CommandLine) - 1) || (Request->BootArgs[Index] > 0x7f)) {
        Print (L"DARWINBOOT: boot args rejected at %lu\n", (UINT64)Index);
        return EFI_BAD_BUFFER_SIZE;
      }
      CommandLine[Index] = (CHAR8)Request->BootArgs[Index];
    }
  }
  mProbeWrite = (AsciiStrStr (CommandLine, "probe-write=1") != NULL);
  mScratchVideo = (AsciiStrStr (CommandLine, "scratch-video=1") != NULL);
  mNoVideo = (AsciiStrStr (CommandLine, "no-video=1") != NULL);
  Root = NULL;
  Image = NULL;
  Tree = NULL;
  TrustCaches = NULL;
  Addon = NULL;
  AddonSize = 0;
  Policies = NULL;
  PolicySize = 0;
  TreePath = NULL;
  Map = NULL;
  Address = 0;
  Capacity = 0;
  Status = gBS->HandleProtocol (Request->Volume, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) { return Status; }
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) { return Status; }
  Mark (L"[0e] volume", 0);
  Mark (L"[0f] read", 0);
  Status = ReadFile (Root, Request->KernelPath, SIZE_128MB, &Image, &ImageSize);
  if (EFI_ERROR (Status)) { goto Fail; }
  Mark (L"[0g] kernel in", 0);
  Status = ParseKernel (Image, ImageSize, &Kernel);
  if (EFI_ERROR (Status)) { goto Fail; }
  Mark (L"[0h] parsed", 0);
  Print (L"DARWINBOOT: kernel %lu bytes, entry %lx base %lx segs %u\r\n",
         (UINT64)ImageSize, (UINT64)Kernel.Entry, (UINT64)Kernel.Base, (UINT32)Kernel.Count);
  // Stall removed for bring-up: timer events are suspects here.
  Length = StrSize (Request->KernelPath);
  // Room for the longest suffix (.trustcache is 11 chars + NUL) with slack.
  TreePath = AllocatePool (Length + 16 * sizeof (CHAR16));
  if (TreePath == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Fail; }
  CopyMem (TreePath, Request->KernelPath, Length);
  StrCatS (TreePath, Length / sizeof (CHAR16) + 16, L".adt");
  Status = ReadFile (Root, TreePath, SIZE_16MB, &Tree, &TreeSize);
  if (EFI_ERROR (Status)) { goto Fail; }
  Mark (L"[0i] adt in", 0);
  Offset = 0;
  RamdiskOffset = MAX_UINTN;
  TrustCacheOffset = MAX_UINTN;
  AddonOffset = MAX_UINTN;
  PolicyOffset = MAX_UINTN;
  WindowSize = 0;
  KeepPacked = FALSE;
  if (!AdtNode (Tree, TreeSize, &Offset, 0, 3, &RamdiskOffset, &TrustCacheOffset, &AddonOffset, &PolicyOffset, &WindowSize, &KeepPacked) || (Offset != TreeSize)) { Status = EFI_COMPROMISED_DATA; goto Fail; }
  if (KeepPacked && (!RamdiskPacked || (RamdiskOffset == MAX_UINTN))) { Status = EFI_COMPROMISED_DATA; goto Fail; }
  if (WindowSize == 0) { WindowSize = WINDOW_SIZE; }
  Mark (L"[0j] tree ok", 0);
  mDbProbe.Step = 3; DbProbeWrite (Request);
  {
    CHAR16 AdtLine[64];
    UnicodeSPrint (AdtLine, sizeof (AdtLine), L"[J1] adt %lx win %lx", (UINT64)TreeSize, (UINT64)WindowSize);
    Mark (AdtLine, 0);
  }
  if (Kernel.Base > MAX_UINT64 - WindowSize) { Status = EFI_LOAD_ERROR; goto Fail; }
  if (Request->Ramdisk != NULL) {
    if ((RamdiskOffset == MAX_UINTN) || (U64 ((UINT8 *)Tree + RamdiskOffset) != 0) ||
        (U64 ((UINT8 *)Tree + RamdiskOffset + 8) != 0)) { Status = EFI_COMPROMISED_DATA; goto Fail; }
  } else if (RamdiskOffset != MAX_UINTN) {
    Status = EFI_UNSUPPORTED;
    goto Fail;
  }
  Mark (L"[J2] ramdisk ok", 0);
  Mark (L"[0k] tc", 0);
  CopyMem (TreePath, Request->KernelPath, Length);
  StrCatS (TreePath, Length / sizeof (CHAR16) + 16, L".trustcache");
  Status = ReadFile (Root, TreePath, SIZE_16MB, &TrustCaches, &TrustCacheSize);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) { goto Fail; }
  Mark (L"[0l] tc in", 0);
  if (TrustCaches != NULL) {
    if ((TrustCacheOffset == MAX_UINTN) || (U64 ((UINT8 *)Tree + TrustCacheOffset) != 0) ||
        (U64 ((UINT8 *)Tree + TrustCacheOffset + 8) != 0) || !ValidTrustCaches (TrustCaches, TrustCacheSize)) {
      Status = EFI_COMPROMISED_DATA;
      goto Fail;
    }
  } else if (TrustCacheOffset != MAX_UINTN) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  //
  // The GPU addon (kernel.macho.gpuaddon, scripts/make-gpu-addon.py): files the kernel writes into the Data volume before its addon
  // mounts. Only its header is checked here; the kernel validates every entry.
  //
  Mark (L"[0m] addon", 0);
  CopyMem (TreePath, Request->KernelPath, Length);
  StrCatS (TreePath, Length / sizeof (CHAR16) + 16, L".gpuaddon");
  Status = ReadFile (Root, TreePath, SIZE_64MB, &Addon, &AddonSize);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) { goto Fail; }
  Mark (L"[0n] addon in", 0);
  if (Addon != NULL) {
    if ((AddonOffset == MAX_UINTN) || (U64 ((UINT8 *)Tree + AddonOffset) != 0) || (U64 ((UINT8 *)Tree + AddonOffset + 8) != 0) ||
        (AddonSize < 24) || (CompareMem (Addon, "NGPUADD2", 8) != 0) || (U64 ((UINT8 *)Addon + 16) != AddonSize)) {
      Status = EFI_COMPROMISED_DATA;
      goto Fail;
    }
  } else if (AddonOffset != MAX_UINTN) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  // Root hash tables live outside the kernel's coalesced constant segments.
  // The kernel authenticates each envelope against its compiled owner anchor.
  Mark (L"[0o] vroot", 0);
  CopyMem (TreePath, Request->KernelPath, Length);
  StrCatS (TreePath, Length / sizeof (CHAR16) + 16, L".vroot");
  Status = ReadFile (Root, TreePath, SIZE_128MB, &Policies, &PolicySize);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) { goto Fail; }
  Mark (L"[0p] vroot in", 0);
  if (Policies != NULL) {
    if ((PolicyOffset == MAX_UINTN) || (U64 ((UINT8 *)Tree + PolicyOffset) != 0) ||
        (U64 ((UINT8 *)Tree + PolicyOffset + 8) != 0) || (PolicySize < 32) ||
        (CompareMem (Policies, "VROOTB01", 8) != 0) || (U64 ((UINT8 *)Policies + 8) != 1) ||
        (U64 ((UINT8 *)Policies + 16) == 0) || (U64 ((UINT8 *)Policies + 16) > 16) ||
        (U64 ((UINT8 *)Policies + 24) != PolicySize)) {
      Status = EFI_COMPROMISED_DATA;
      goto Fail;
    }
  } else if (PolicyOffset != MAX_UINTN) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  Mark (L"[1] assets", 0);
  Print (L"DARWINBOOT: p1 assets trustcache %lu addon %lu vroot %lu\r\n",
         (UINT64)TrustCacheSize, (UINT64)AddonSize, (UINT64)PolicySize);
  mDbProbe.Step = 4; DbProbeWrite (Request);
  Status = ReadBootVideo (Video);
  if (EFI_ERROR (Status)) { goto Fail; }
  Mark (L"[2] video", 0);
  Print (L"DARWINBOOT: p2 video base %lx\r\n", (UINT64)Video[0]);
  mDbProbe.Step = 5; DbProbeWrite (Request);
  Status = AllocateWindow (&Kernel, WindowSize, &Address);
  if (EFI_ERROR (Status)) { goto Fail; }
  Mark (L"[3] window", 0);
  Print (L"DARWINBOOT: p3 window at %lx size %lx\r\n", (UINT64)Address, (UINT64)WindowSize);

  ArgsAddress = ALIGN_VALUE (Address + Kernel.End - Kernel.Base, SIZE_16KB);
  TreeAddress = ArgsAddress + EFI_PAGE_SIZE;
  RamdiskAddress = ALIGN_VALUE (TreeAddress + TreeSize, EFI_PAGE_SIZE);
  RamdiskBytes = ALIGN_VALUE (KeepPacked ? Request->RamdiskSize : RamdiskExpandedSize, EFI_PAGE_SIZE);
  TrustCacheAddress = RamdiskAddress + RamdiskBytes;
  AddonAddress = ALIGN_VALUE (TrustCacheAddress + TrustCacheSize, SIZE_16KB);
  PolicyAddress = ALIGN_VALUE (AddonAddress + AddonSize, SIZE_16KB);
  Used = ALIGN_VALUE (PolicyAddress + PolicySize, SIZE_16KB) - Address;
  if (Used > WindowSize - BLOCK_SIZE) {
    Print (L"DARWINBOOT: used %lu window %lu kernel %lu ramdisk %lu\n", (UINT64)Used, (UINT64)WindowSize, (UINT64)(Kernel.End - Kernel.Base), (UINT64)RamdiskBytes);
    Status = EFI_BAD_BUFFER_SIZE;
    goto Fail;
  }
  Mark (L"[4] layout", 0);
  Print (L"DARWINBOOT: p4 window layout used %lu of %lx\r\n", (UINT64)Used, (UINT64)WindowSize);
  mDbProbe.Step = 6; DbProbeWrite (Request);
  Mark (L"[5] zero", 0);
  Print (L"DARWINBOOT: p5 zeroing %lu bytes\r\n", (UINT64)Used);
  ZeroMem ((VOID *)(UINTN)Address, (UINTN)Used);
  Mark (L"[6] copy", 0);
  Print (L"DARWINBOOT: p6 copying %u segments\r\n", (UINT32)Kernel.Count);
  for (Index = 0; Index < Kernel.Count; Index++) {
    CopyMem ((VOID *)(UINTN)(Address + Kernel.Segments[Index].Address - Kernel.Base),
             (UINT8 *)Image + Kernel.Segments[Index].FileOffset, (UINTN)Kernel.Segments[Index].FileSize);
  }
  Mark (L"[7] copied", 0);
  Print (L"DARWINBOOT: p7 kernel image in place\r\n");
  Args = (DARWIN_BOOT_ARGS *)(UINTN)ArgsAddress;
  Args->Revision = 2;
  Args->Version = 2;
  Args->VirtualBase = Kernel.Base;
  Args->PhysicalBase = Address;
  Args->MemorySize = WindowSize;
  Args->MemorySizeActual = WindowSize;
  Args->TopOfKernelData = Address + Used;
  if (mScratchVideo) {
    // A framebuffer inside the window, addressed through the linear map: the
    // kernel's video console writes it as a virtual address, so a raw physical
    // address (what the GOP gives) would fault.  With scratch-video=1 the panel
    // keeps whatever this loader drew and the kernel paints into RAM instead.
    UINT64 Scratch = ALIGN_VALUE (Address + Used, EFI_PAGE_SIZE);
    UINT64 Bytes   = (UINT64)SCRATCH_ROW * SCRATCH_HEIGHT;
    if (Scratch + Bytes < Address + WindowSize) {
      ZeroMem ((VOID *)(UINTN)Scratch, (UINTN)Bytes);
      Video[0] = Kernel.Base + (Scratch - Address);
      Video[1] = 1;
      Video[2] = SCRATCH_ROW;
      Video[3] = SCRATCH_ROW / 4;
      Video[4] = SCRATCH_HEIGHT;
      Video[5] = 32;
      mVideoBarBase = Scratch;
      Args->TopOfKernelData = Scratch + Bytes;
      Print (L"DARWINBOOT: scratch video pa %lx va %lx %lux%lu\r\n",
             Scratch, (UINT64)Video[0], (UINT64)Video[3], (UINT64)SCRATCH_HEIGHT);
    } else {
      Print (L"DARWINBOOT: scratch video does not fit\r\n");
    }
  }
  if (mNoVideo) {
    // Hand the kernel no video at all: initialize_screen then takes its
    // "No video - forcing serial mode" path instead of mapping a framebuffer.
    ZeroMem (Video, sizeof (Video));
    Print (L"DARWINBOOT: video suppressed by no-video=1\r\n");
  }
  CopyMem (Args->Video, Video, sizeof (Video));
  Args->DeviceTree = Kernel.Base + TreeAddress - Address;
  Args->DeviceTreeLength = (UINT32)TreeSize;
  CopyMem (Args->CommandLine, CommandLine, sizeof (CommandLine));
  CopyMem ((VOID *)(UINTN)TreeAddress, Tree, TreeSize);
  if (Request->Ramdisk != NULL) {
    Mark (L"[8] ramdisk", 0);
    Print (L"DARWINBOOT: p8 ramdisk %lu bytes packed %u\r\n", (UINT64)Request->RamdiskSize, (UINT32)KeepPacked);
    if (KeepPacked) { CopyMem ((VOID *)(UINTN)RamdiskAddress, Request->Ramdisk, Request->RamdiskSize); }
    else { CopyRamdisk ((VOID *)(UINTN)RamdiskAddress, Request->Ramdisk, Request->RamdiskSize, RamdiskPacked); }
    Mark (L"[9] ramdone", 0);
    Print (L"DARWINBOOT: p9 ramdisk in place\r\n");
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + RamdiskOffset), RamdiskAddress);
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + RamdiskOffset + 8), KeepPacked ? Request->RamdiskSize : RamdiskBytes);
  }
  if (TrustCaches != NULL) {
    CopyMem ((VOID *)(UINTN)TrustCacheAddress, TrustCaches, TrustCacheSize);
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + TrustCacheOffset), TrustCacheAddress);
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + TrustCacheOffset + 8), TrustCacheSize);
    FreePool (TrustCaches); TrustCaches = NULL;
  }
  if (Addon != NULL) {
    CopyMem ((VOID *)(UINTN)AddonAddress, Addon, AddonSize);
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + AddonOffset), AddonAddress);
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + AddonOffset + 8), AddonSize);
    FreePool (Addon); Addon = NULL;
  }
  if (Policies != NULL) {
    CopyMem ((VOID *)(UINTN)PolicyAddress, Policies, PolicySize);
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + PolicyOffset), PolicyAddress);
    WriteUnaligned64 ((UINT64 *)(UINTN)(TreeAddress + PolicyOffset + 8), PolicySize);
    FreePool (Policies); Policies = NULL;
  }
  Mark (L"[10] step7", 0);
  Print (L"DARWINBOOT: p10 assets copied, writing step 7\r\n");
  mDbProbe.Step = 7; DbProbeWrite (Request);
  Mark (L"[11] root", 0);
  Print (L"DARWINBOOT: p11 closing the root\r\n");
  FreePool (Image); Image = NULL;
  FreePool (Tree); Tree = NULL;
  FreePool (TreePath); TreePath = NULL;
  Root->Close (Root); Root = NULL;
  Status = GetMap (&Map, &Capacity, &MapSize, &Key, &Stride);
  if (EFI_ERROR (Status)) { goto Fail; }
  Mark (L"[12] map", 0);
  Print (L"DARWINBOOT: p12 map %lu bytes key %lx\r\n", (UINT64)MapSize, (UINT64)Key);
  Print (L"DARWINBOOT: ARM64 va=0x%lx pa=0x%lx args=0x%lx entry=0x%lx owned=0x%lx\n",
         Kernel.Base, Address, ArgsAddress, Address + Kernel.Entry - Kernel.Base, (UINT64)WindowSize);
  Print (L"DARWINBOOT: video base=0x%lx display=%lu row=%lu width=%lu height=%lu depth=%lu\n",
         Video[0], Video[1], Video[2], Video[3], Video[4], Video[5]);
  Print (L"DARWINBOOT: command line: %a\n", CommandLine);
  mDbProbe.Step = 9; DbProbeWrite (Request);
  Print (L"DARWINBOOT: inputs validated; exiting boot services\n");

  //
  // The same facts in large type over the whole screen. The firmware's log
  // draws far larger than the text console, and the form feed clears the
  // screen first. This is what stays visible if the kernel never draws.
  //
  DEBUG ((DEBUG_ERROR, "\fDARWINBOOT: hand-off to the kernel\n\n"));
  DEBUG ((DEBUG_ERROR, "kernel   va 0x%lx  pa 0x%lx\n", Kernel.Base, Address));
  DEBUG ((DEBUG_ERROR, "entry    pa 0x%lx\n", Address + Kernel.Entry - Kernel.Base));
  DEBUG ((DEBUG_ERROR, "args     pa 0x%lx  window 0x%lx bytes\n", ArgsAddress, (UINT64)WindowSize));
  DEBUG ((DEBUG_ERROR, "video    base 0x%lx  %lux%lu  row %lu bytes  %lu bpp\n", Video[0], Video[3], Video[4], Video[2], Video[5]));
  DEBUG ((DEBUG_ERROR, "entry words %08x %08x %08x %08x\n",
          ReadUnaligned32 ((CONST UINT32 *)(UINTN)(Address + Kernel.Entry - Kernel.Base)),
          ReadUnaligned32 ((CONST UINT32 *)(UINTN)(Address + Kernel.Entry - Kernel.Base + 4)),
          ReadUnaligned32 ((CONST UINT32 *)(UINTN)(Address + Kernel.Entry - Kernel.Base + 8)),
          ReadUnaligned32 ((CONST UINT32 *)(UINTN)(Address + Kernel.Entry - Kernel.Base + 12))));
  DEBUG ((DEBUG_ERROR, "CurrentEL %lu  SCTLR_EL1 0x%lx  TCR_EL1 0x%lx\n", DarwinCurrentEl () >> 2, READ_SYSREG (sctlr_el1), READ_SYSREG (tcr_el1)));
  DEBUG ((DEBUG_ERROR, "ID_AA64MMFR0_EL1 0x%lx  (PARange %lu, 4 KiB granule %a)\n", READ_SYSREG (id_aa64mmfr0_el1),
          READ_SYSREG (id_aa64mmfr0_el1) & 0xF, (((READ_SYSREG (id_aa64mmfr0_el1) >> 28) & 0xF) == 0xF) ? "NOT supported" : "supported"));
  DEBUG ((DEBUG_ERROR, "ID_AA64PFR0_EL1  0x%lx\n", READ_SYSREG (id_aa64pfr0_el1)));
  DEBUG ((DEBUG_ERROR, "\nExiting boot services. A green bar across the top\nmarks the loader handoff before the kernel branch.\n"));
  Print (L"DARWINBOOT: p13 map, then straight to ExitBootServices\r\n");
  for (Attempt = 0; Attempt < 3; Attempt++) {
    MapSize = Capacity;
    Status = gBS->GetMemoryMap (&MapSize, Map, &Key, &Stride, &Version);
    if (EFI_ERROR (Status)) {
      Print (L"DARWINBOOT: GetMemoryMap failed %r\r\n", Status);
      if (Attempt == 0) { goto Fail; }
      CpuDeadLoop ();
    }
    //
    // Nothing between GetMemoryMap and ExitBootServices may allocate: a Mark,
    // a Print or the probe write all change the map and invalidate Key, which
    // is exactly the EFI_INVALID_PARAMETER loop seen on hardware.
    //
    Status = gBS->ExitBootServices (mImage, Key);
    if (!EFI_ERROR (Status)) {
      // Boot services are gone: Print/Mark/DbProbeFlush are all invalid here,
      // only the cached framebuffer can still draw.
      mBsGone = TRUE;
      DiagDraw (L"[16] EBS ok", 0);
      DiagDraw (L"[15] jump", 0);
      if (AsciiStrStr (CommandLine, "qemu-mark=1") != NULL) {
        QemuMark ("QEMU: EBS ok, entering the kernel\n");
      }
      // The green bar is drawn with the firmware's own page tables still active,
      // so it must use an address the firmware can write: the physical base.
      DarwinEnter (ArgsAddress, Address + Kernel.Entry - Kernel.Base, Address, WindowSize,
                   mVideoBarBase != 0 ? mVideoBarBase : Video[0], Video[2] * Video[4]);
      CpuDeadLoop ();
    }
    Print (L"DARWINBOOT: exit boot services attempt %u -> %r\r\n", Attempt, Status);
    if (Status != EFI_INVALID_PARAMETER) {
      Print (L"DARWINBOOT: exit boot services refused with %r, stopping\r\n", Status);
      CpuDeadLoop ();
    }
  }
  CpuDeadLoop ();
  return EFI_ABORTED;
Fail:
  {
    CHAR16 FailText[64];
    UnicodeSPrint (FailText, sizeof (FailText), L"[FAIL] %r", Status);
    Print (L"MARK-FAIL %s\r\n", FailText);
    for (;;) {
      DiagDraw (FailText, 0);
      gBS->Stall (200000);
    }
  }
  if (Root != NULL) { Root->Close (Root); }
  if (Image != NULL) { FreePool (Image); }
  if (Tree != NULL) { FreePool (Tree); }
  if (TrustCaches != NULL) { FreePool (TrustCaches); }
  if (Addon != NULL) { FreePool (Addon); }
  if (Policies != NULL) { FreePool (Policies); }
  if (TreePath != NULL) { FreePool (TreePath); }
  if (Map != NULL) { FreePool (Map); }
  if (Address != 0) { gBS->FreePages (Address, EFI_SIZE_TO_PAGES (WindowSize)); }
  Print (L"DARWINBOOT: boot refused: %r; firmware retained\n", Status);
  return Status;
}

STATIC UNIXNU_LOADER_PROTOCOL mLoader = { UNIXNU_LOADER_REVISION, Boot };

STATIC
EFI_STATUS
EFIAPI
Unload (EFI_HANDLE ImageHandle)
{
  return gBS->UninstallMultipleProtocolInterfaces (ImageHandle, &gUniXNULoaderProtocolGuid, &mLoader, NULL);
}

EFI_STATUS
EFIAPI
DarwinBootEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_STATUS               Status;
  VOID                     *Existing;

  (VOID)SystemTable;
  DEBUG ((DEBUG_ERROR, "DARWINBOOT: entry called\n"));
  // Stall removed for bring-up: timer events are suspects here.
  Status = gBS->LocateProtocol (&gUniXNULoaderProtocolGuid, NULL, &Existing);
  DEBUG ((DEBUG_ERROR, "DARWINBOOT: existing loader -> %r\n", Status));
  // Stall removed for bring-up: timer events are suspects here.
  if (!EFI_ERROR (Status)) { return EFI_ALREADY_STARTED; }
  DEBUG ((DEBUG_ERROR, "DARWINBOOT: handle loaded-image\n"));
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  DEBUG ((DEBUG_ERROR, "DARWINBOOT: loaded-image -> %r\n", Status));
  if (EFI_ERROR (Status)) { return Status; }
  mImage = ImageHandle;
  Status = gBS->InstallMultipleProtocolInterfaces (&ImageHandle, &gUniXNULoaderProtocolGuid, &mLoader, NULL);
  DEBUG ((DEBUG_ERROR, "DARWINBOOT: install loader -> %r\n", Status));
  if (!EFI_ERROR (Status)) {
    Loaded->Unload = Unload;
    DEBUG ((DEBUG_ERROR, "DARWINBOOT: loader v2 installed EL%u\n", (UINT32)(DarwinCurrentEl () >> 2)));
  }
  return Status;
}
