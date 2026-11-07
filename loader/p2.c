// UniXNU efisp loader - phase 2 (self-contained EFI application, ARM64 PE32+).
//
// Read-only probe used from the efisp slot:
//   1. enumerate EFI_BLOCK_IO_PROTOCOL handles and dump per-handle diagnostics
//   2. parse whole-disk GPT (LBA1) and find the partition named "userdata"
//   3. read the first PAYLOAD_MAX bytes of that partition into page memory
//   4. build a minimal flattened device tree: /chosen { linux,initrd-start, linux,initrd-end }
//   5. print each step on the UEFI console and halt
//
// Phase 3 will copy the firmware volume to 0xFF300000 and branch into SEC.

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef UINT64             UINTN;
typedef UINTN              EFI_STATUS;
typedef void              *EFI_HANDLE;
typedef UINT16             CHAR16;

#define EFIAPI
#define BY_PROTOCOL      2
#define EFI_LOADER_DATA  2
#define PAYLOAD_MAX (64ULL * 1024 * 1024)
#define CHUNK        (1ULL * 1024 * 1024)

typedef struct { UINT32 a; UINT16 b; UINT16 c; UINT8 d[8]; } EFI_GUID;

typedef struct {
    UINT64 Signature; UINT32 Revision; UINT32 HeaderSize; UINT32 CRC32; UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *GetTime, *SetTime, *GetWakeupTime, *SetWakeupTime, *SetVirtualAddressMap, *ConvertPointer;
    void *GetVariable, *GetNextVariableName, *SetVariable, *GetNextHighMonotonicCount, *ResetSystem;
    void *UpdateCapsule, *QueryCapsuleCapabilities, *QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

typedef struct {
    void *Reset, *OutputString, *TestString, *QueryMode, *SetMode, *SetAttribute, *ClearScreen;
    void *SetCursorPosition, *EnableCursor, *Mode;
} EFI_SIMPLE_TEXT_OUTPUT;

typedef struct { void *Reset, *ReadKeyStroke, *WaitForKey; } EFI_SIMPLE_TEXT_INPUT;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL, *RestoreTPL, *AllocatePages, *FreePages, *GetMemoryMap, *AllocatePool, *FreePool;
    void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
    void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
    void *HandleProtocol, *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath;
    void *InstallConfigurationTable, *LoadImage, *StartImage, *Exit, *UnloadImage, *ExitBootServices;
    void *GetNextMonotonicCount, *Stall, *SetWatchdogTimer, *ConnectController, *DisconnectController;
    void *OpenProtocol, *CloseProtocol, *OpenProtocolInformation, *ProtocolsPerHandle, *LocateHandleBuffer;
    void *LocateProtocol, *InstallMultipleProtocolInterfaces, *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32, *CopyMem, *SetMem, *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor; UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle; EFI_SIMPLE_TEXT_INPUT *ConIn;
    EFI_HANDLE ConsoleOutHandle; EFI_SIMPLE_TEXT_OUTPUT *ConOut;
    EFI_HANDLE StandardErrorHandle; EFI_SIMPLE_TEXT_OUTPUT *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices; EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries; void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    UINT32 MediaId;
    UINT8 RemovableMedia, MediaPresent, LogicalPartition, ReadOnly, WriteCaching;
    UINT8 _pad[3];
    UINT32 IoAlign;
    UINT64 LastBlock;
    UINT64 LowestAlignedLba;
    UINT32 LogicalBlocksPerPhysicalBlock;
    UINT32 OptimalTransferLengthGranularity;
    UINT32 BlockSize;
    UINT32 _pad2;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL_S EFI_BLOCK_IO_PROTOCOL;
struct EFI_BLOCK_IO_PROTOCOL_S {
    UINT64 Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    void *Reset;
    EFI_STATUS (*ReadBlocks)(EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, UINT64 Lba, UINTN BufferSize, void *Buffer);
    void *WriteBlocks;
    void *FlushBlocks;
};

typedef EFI_STATUS (*EFI_LOCATE_HANDLE_BUFFER)(UINT32 SearchType, EFI_GUID *Protocol, void *SearchKey, UINTN *NoHandles, EFI_HANDLE **Buffer);
typedef EFI_STATUS (*EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface);
typedef EFI_STATUS (*EFI_ALLOCATE_POOL)(UINT32 PoolType, UINTN Size, void **Buffer);
typedef EFI_STATUS (*EFI_ALLOCATE_PAGES)(UINT32 Type, UINT32 MemoryType, UINTN Pages, UINT64 *Memory);
typedef EFI_STATUS (*EFI_STALL)(UINTN Microseconds);

// {964e5b21-6459-11d2-8e39-00a0c969723b} EFI_BLOCK_IO_PROTOCOL
static EFI_GUID gBlockIoGuid = { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

static EFI_SYSTEM_TABLE *gST;

// Force a .reloc section: EFI LoadImage rejects images without relocations.
__attribute__((used)) static void *gSelfRef = (void *)&gSelfRef;

static void out_ascii(const char *s)
{
    CHAR16 b[160];
    UINTN i = 0;
    while (s[i] && i < 159) { b[i] = (CHAR16)(UINT8)s[i]; i++; }
    b[i] = 0;
    if (gST && gST->ConOut && gST->ConOut->OutputString)
        ((EFI_STATUS (*)(EFI_SIMPLE_TEXT_OUTPUT *, CHAR16 *))gST->ConOut->OutputString)(gST->ConOut, b);
}

static void out_hex(UINT64 v)
{
    char t[20];
    const char *h = "0123456789abcdef";
    int i;
    t[0] = '0'; t[1] = 'x';
    for (i = 0; i < 16; i++) t[2 + i] = h[(v >> (60 - 4 * i)) & 0xf];
    t[18] = 0;
    out_ascii(t);
}

static int u16eq_ascii(const CHAR16 *u, const char *a)
{
    int i = 0;
    while (a[i]) {
        if (u[i] != (CHAR16)(UINT8)a[i]) return 0;
        i++;
    }
    return u[i] == 0;
}

// ---- flattened device tree writer -----------------------------------------

static void be32(UINT8 **p, UINT32 v)
{
    (*p)[0] = (UINT8)(v >> 24); (*p)[1] = (UINT8)(v >> 16); (*p)[2] = (UINT8)(v >> 8); (*p)[3] = (UINT8)v;
    *p += 4;
}

static void be64(UINT8 **p, UINT64 v)
{
    be32(p, (UINT32)(v >> 32));
    be32(p, (UINT32)v);
}

static UINT8 *fdt_build(UINT64 initrd_start, UINT64 initrd_end, UINT64 *size_out)
{
    static UINT8 structb[3072];
    static UINT8 strings[512];
    static UINT8 out[4096];
    UINT8 *p = structb;
    UINT32 slen = 0;
    UINT32 i;

    UINT32 o_compat, o_acells, o_scells, o_is, o_ie;
    {
        const char *n;
        n = "compatible";         o_compat = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "#address-cells";     o_acells = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "#size-cells";        o_scells = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "linux,initrd-start"; o_is     = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "linux,initrd-end";   o_ie     = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
    }

    be32(&p, 1); be32(&p, 0);                       // begin root ""
    { const char *v = "nx741j"; UINT32 l = 7; be32(&p, 3); be32(&p, l); be32(&p, o_compat);
      for (i = 0; i < l; i++) *p++ = (UINT8)v[i]; }
    while (((UINTN)p - (UINTN)structb) & 3) *p++ = 0;
    be32(&p, 3); be32(&p, 4); be32(&p, o_acells); be32(&p, 2);
    be32(&p, 3); be32(&p, 4); be32(&p, o_scells); be32(&p, 2);
    be32(&p, 1); { const char *n = "chosen"; while (*n) *p++ = (UINT8)*n++; *p++ = 0; }
    while (((UINTN)p - (UINTN)structb) & 3) *p++ = 0;
    be32(&p, 3); be32(&p, 8); be32(&p, o_is); be64(&p, initrd_start);
    be32(&p, 3); be32(&p, 8); be32(&p, o_ie); be64(&p, initrd_end);
    be32(&p, 2); be32(&p, 2); be32(&p, 9);

    UINT32 struct_size = (UINT32)(p - structb);
    UINT32 off_struct = 56;
    UINT32 off_strings = off_struct + struct_size;
    UINT32 total = (off_strings + slen + 3) & ~3u;

    UINT8 *w = out;
    be32(&w, 0xd00dfeed); be32(&w, total); be32(&w, off_struct); be32(&w, off_strings);
    be32(&w, 40); be32(&w, 17); be32(&w, 16); be32(&w, 0); be32(&w, slen); be32(&w, struct_size);
    be64(&w, 0); be64(&w, 0);
    for (i = 0; i < struct_size; i++) out[off_struct + i] = structb[i];
    for (i = 0; i < slen; i++) out[off_strings + i] = strings[i];
    for (i = off_strings + slen; i < total; i++) out[i] = 0;

    *size_out = total;
    return out;
}

// ---- main ------------------------------------------------------------------

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    (void)image;
    gST = st;
    out_ascii("\r\n[UNIXNU] efisp loader phase 2\r\n");
    if (!st || !st->BootServices) { out_ascii("[UNIXNU] no boot services\r\n"); for (;;) { } }

    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_LOCATE_HANDLE_BUFFER locate = (EFI_LOCATE_HANDLE_BUFFER)bs->LocateHandleBuffer;
    EFI_HANDLE_PROTOCOL handle_proto = (EFI_HANDLE_PROTOCOL)bs->HandleProtocol;
    EFI_ALLOCATE_PAGES allocp = (EFI_ALLOCATE_PAGES)bs->AllocatePages;
    EFI_STALL stall = (EFI_STALL)bs->Stall;

    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    EFI_STATUS s = locate(BY_PROTOCOL, &gBlockIoGuid, 0, &count, &handles);
    out_ascii("[UNIXNU] blockio handles: "); out_hex(s ? 0 : count); out_ascii("\r\n");
    if (s || count == 0) { for (;;) { stall(1000000); } }

    // UFS BlockIo requires 4 KiB aligned buffers.
    __attribute__((aligned(4096))) static UINT8 sector[16384];
    UINT64 userdata_lba = 0;
    EFI_BLOCK_IO_PROTOCOL *userdata_io = 0;
    UINT32 block_size = 0;

    for (UINTN i = 0; i < count && i < 10; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio = 0;
        if (handle_proto(handles[i], &gBlockIoGuid, (void **)&bio) || !bio || !bio->Media) continue;
        EFI_BLOCK_IO_MEDIA *m = bio->Media;

        out_ascii("[UNIXNU] io "); out_hex(i);
        out_ascii(" bs "); out_hex(m->BlockSize);
        out_ascii(" last "); out_hex(m->LastBlock);
        out_ascii(" part "); out_hex(m->LogicalPartition);
        out_ascii("\r\n");

        if (m->BlockSize < 512 || m->BlockSize > 8192) continue;
        if (m->LogicalPartition) continue;

        s = bio->ReadBlocks(bio, m->MediaId, 1, m->BlockSize, sector);
        if (s) { out_ascii("[UNIXNU]   read lba1 failed "); out_hex(s); out_ascii("\r\n"); continue; }
        out_ascii("[UNIXNU]   lba1 word "); out_hex(*(UINT32 *)sector); out_ascii("\r\n");
        if (!(sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' && sector[3] == ' ')) continue;

        UINT64 ent_lba = *(UINT64 *)(sector + 72);
        UINT32 nent = *(UINT32 *)(sector + 80);
        UINT32 esz = *(UINT32 *)(sector + 84);
        out_ascii("[UNIXNU]   gpt entries "); out_hex(nent);
        out_ascii(" size "); out_hex(esz); out_ascii(" at "); out_hex(ent_lba); out_ascii("\r\n");
        if (esz < 128 || esz > 512 || nent == 0 || nent > 256) continue;

        UINT64 ent_addr = 0;
        UINTN ent_pages = ((UINTN)nent * esz + 4095) / 4096;
        if (allocp(0, EFI_LOADER_DATA, ent_pages, &ent_addr) || !ent_addr) {
            out_ascii("[UNIXNU]   entries alloc failed\r\n"); continue;
        }
        UINT8 *ents = (UINT8 *)ent_addr;
        if (bio->ReadBlocks(bio, m->MediaId, ent_lba, (UINTN)nent * esz, ents)) {
            out_ascii("[UNIXNU]   entries read failed\r\n"); continue;
        }

        for (UINT32 e = 0; e < nent; e++) {
            UINT8 *ep = ents + (UINTN)e * esz;
            CHAR16 *name = (CHAR16 *)(ep + 56);
            if (u16eq_ascii(name, "userdata")) {
                userdata_lba = *(UINT64 *)(ep + 32);
                userdata_io = bio;
                block_size = m->BlockSize;
                break;
            }
        }
        if (userdata_io) break;
    }

    if (!userdata_io) { out_ascii("[UNIXNU] userdata partition not found\r\n"); for (;;) { stall(1000000); } }
    out_ascii("[UNIXNU] userdata lba: "); out_hex(userdata_lba); out_ascii("\r\n");

    UINT64 payload_addr = 0;
    UINTN payload_pages = (UINTN)(PAYLOAD_MAX / 4096);
    s = allocp(0, EFI_LOADER_DATA, payload_pages, &payload_addr);
    out_ascii("[UNIXNU] payload alloc: "); out_hex(s); out_ascii(" @ "); out_hex(payload_addr); out_ascii("\r\n");
    if (s || !payload_addr) { out_ascii("[UNIXNU] cannot allocate payload buffer\r\n"); for (;;) { stall(1000000); } }

    UINT8 *payload = (UINT8 *)payload_addr;
    UINT64 done = 0;
    while (done < PAYLOAD_MAX) {
        UINT64 chunk = CHUNK;
        if (chunk > PAYLOAD_MAX - done) chunk = PAYLOAD_MAX - done;
        UINT64 lba = userdata_lba + done / block_size;
        if (userdata_io->ReadBlocks(userdata_io, userdata_io->Media->MediaId, lba, (UINTN)chunk, payload + done)) {
            out_ascii("[UNIXNU] payload read failed at "); out_hex(done); out_ascii("\r\n"); break;
        }
        done += chunk;
    }
    out_ascii("[UNIXNU] payload bytes: "); out_hex(done); out_ascii(" @ "); out_hex((UINT64)payload); out_ascii("\r\n");

    UINT64 fdt_size = 0;
    UINT8 *fdt = fdt_build((UINT64)payload, (UINT64)payload + done, &fdt_size);
    out_ascii("[UNIXNU] fdt built: "); out_hex(fdt_size); out_ascii(" @ "); out_hex((UINT64)fdt); out_ascii("\r\n");
    out_ascii("[UNIXNU] phase 2 done, halting\r\n");
    for (;;) { stall(1000000); }
    return 0;
}
