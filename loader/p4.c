// UniXNU efisp loader - phase 3 (ARM64 PE32+).
//
//   1. GPT: find "rawdump" (payload at +0, firmware volume at +64 MiB)
//   2. read 64 MiB payload and 4 MiB firmware volume into page memory
//   3. build FDT with /chosen/linux,initrd-start/end
//   4. copy the firmware volume to 0xFF300000, clean caches, verify read-back
//   5. print everything, then branch into the FD (x0 = FDT) which starts with a `b`
//      instruction to the SEC entry.

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
#define FD_MAX      (4ULL * 1024 * 1024)
#define FD_SECTORS  (FD_MAX / 4096)          // 1024
#define CHUNK       (1ULL * 1024 * 1024)
#define FD_BASE     0xff300000ULL

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
typedef EFI_STATUS (*EFI_ALLOCATE_PAGES)(UINT32 Type, UINT32 MemoryType, UINTN Pages, UINT64 *Memory);
typedef EFI_STATUS (*EFI_STALL)(UINTN Microseconds);
typedef EFI_STATUS (*EFI_GET_MEMORY_MAP)(UINTN *Size, void *Map, UINTN *MapKey, UINTN *DescSize, UINT32 *DescVersion);
typedef struct { UINT32 Type; UINT32 Pad; UINT64 Phys; UINT64 Virt; UINT64 Pages; UINT64 Attr; } MEM_DESC;

// {964e5b21-6459-11d2-8e39-00a0c969723b} EFI_BLOCK_IO_PROTOCOL
static EFI_GUID gBlockIoGuid = { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

static EFI_SYSTEM_TABLE *gST;
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
    while (a[i]) { if (u[i] != (CHAR16)(UINT8)a[i]) return 0; i++; }
    return u[i] == 0;
}

// ---- FDT -------------------------------------------------------------------

static void be32(UINT8 **p, UINT32 v)
{
    (*p)[0] = (UINT8)(v >> 24); (*p)[1] = (UINT8)(v >> 16); (*p)[2] = (UINT8)(v >> 8); (*p)[3] = (UINT8)v;
    *p += 4;
}

static void be64(UINT8 **p, UINT64 v) { be32(p, (UINT32)(v >> 32)); be32(p, (UINT32)v); }

static UINT8 *fdt_build(UINT64 initrd_start, UINT64 initrd_end, UINT64 *size_out)
{
    static UINT8 structb[3072];
    static UINT8 strings[512];
    static UINT8 out[4096];
    UINT8 *p = structb;
    UINT32 slen = 0, i;
    UINT32 o_compat, o_acells, o_scells, o_is, o_ie;
    {
        const char *n;
        n = "compatible";         o_compat = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "#address-cells";     o_acells = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "#size-cells";        o_scells = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "linux,initrd-start"; o_is     = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
        n = "linux,initrd-end";   o_ie     = slen; while (*n) strings[slen++] = (UINT8)*n++; strings[slen++] = 0;
    }
    be32(&p, 1); be32(&p, 0);
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

// ---- cache + branch --------------------------------------------------------

static void sync_caches(UINT64 start, UINT64 size)
{
    UINT64 ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    UINT32 lsz = (UINT32)(4u << ((ctr >> 16) & 0xf));
    UINT64 p = start & ~(UINT64)(lsz - 1);
    for (; p < start + size; p += lsz)
        __asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy\n ic iallu\n dsb sy\n isb" ::: "memory");
}

static void jump_to_fd(UINT64 fd, UINT64 fdt) __attribute__((noreturn));
static void jump_to_fd(UINT64 fd, UINT64 fdt)
{
    __asm__ volatile(
        "mov x0, %0\n"
        "mov x1, xzr\n"
        "mov x2, xzr\n"
        "mov x3, xzr\n"
        "br  %1\n"
        :: "r"(fdt), "r"(fd) : "memory");
    for (;;) { }
}

// Turn the MMU and the data/instruction caches off at the current exception level,
// so the firmware volume entry sees the same environment as on the Linux kernel path.
static void disable_mmu_caches(void)
{
    UINT64 el, sctlr;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    el = (el >> 2) & 3;
    if (el >= 2) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
        sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
        __asm__ volatile("tlbi alle2\n dsb sy\n isb" ::: "memory");
    } else {
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
        __asm__ volatile("tlbi vmalle1\n dsb sy\n isb" ::: "memory");
    }
    __asm__ volatile("ic iallu\n dsb sy\n isb" ::: "memory");
}

static UINT64 current_el(void) { UINT64 v; __asm__ volatile("mrs %0, CurrentEL" : "=r"(v)); return (v >> 2) & 3; }
static UINT64 read_sctlr(void)
{
    UINT64 v = 0;
    if (current_el() >= 2) __asm__ volatile("mrs %0, sctlr_el2" : "=r"(v));
    else                   __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}

// ---- main ------------------------------------------------------------------

static EFI_BLOCK_IO_PROTOCOL *gIo;
static UINT32 gBlockSize;
__attribute__((aligned(4096))) static UINT8 gSector[16384];

static int find_partition(const char *want, UINT64 *out_lba)
{
    EFI_BOOT_SERVICES *bs = gST->BootServices;
    EFI_LOCATE_HANDLE_BUFFER locate = (EFI_LOCATE_HANDLE_BUFFER)bs->LocateHandleBuffer;
    EFI_HANDLE_PROTOCOL handle_proto = (EFI_HANDLE_PROTOCOL)bs->HandleProtocol;
    EFI_ALLOCATE_PAGES allocp = (EFI_ALLOCATE_PAGES)bs->AllocatePages;

    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    if (locate(BY_PROTOCOL, &gBlockIoGuid, 0, &count, &handles) || count == 0) return 0;

    for (UINTN i = 0; i < count && i < 10; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio = 0;
        if (handle_proto(handles[i], &gBlockIoGuid, (void **)&bio) || !bio || !bio->Media) continue;
        UINT32 cands[3];
        cands[0] = bio->Media->BlockSize; cands[1] = 4096; cands[2] = 512;
        for (int c = 0; c < 3; c++) {
            UINT32 bsz = cands[c];
            if (bsz < 512 || bsz > 8192) continue;
            if (c > 0 && bsz == cands[0]) continue;
            if (bio->ReadBlocks(bio, bio->Media->MediaId, 1, bsz, gSector)) continue;
            if (!(gSector[0] == 'E' && gSector[1] == 'F' && gSector[2] == 'I' && gSector[3] == ' ')) continue;

            UINT64 ent_lba = *(UINT64 *)(gSector + 72);
            UINT32 nent = *(UINT32 *)(gSector + 80);
            UINT32 esz = *(UINT32 *)(gSector + 84);
            if (esz < 128 || esz > 512 || nent == 0 || nent > 256) continue;

            UINT64 ent_addr = 0;
            UINTN ent_pages = ((UINTN)nent * esz + 4095) / 4096;
            if (allocp(0, EFI_LOADER_DATA, ent_pages, &ent_addr) || !ent_addr) continue;
            UINT8 *ents = (UINT8 *)ent_addr;
            if (bio->ReadBlocks(bio, bio->Media->MediaId, ent_lba, (UINTN)nent * esz, ents)) continue;

            for (UINT32 e = 0; e < nent; e++) {
                UINT8 *ep = ents + (UINTN)e * esz;
                if (u16eq_ascii((CHAR16 *)(ep + 56), want)) {
                    *out_lba = *(UINT64 *)(ep + 32);
                    gIo = bio;
                    gBlockSize = bsz;
                    return 1;
                }
            }
        }
    }
    return 0;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    (void)image;
    gST = st;
    out_ascii("\r\n[UNIXNU] efisp loader phase 3\r\n");
    if (!st || !st->BootServices) { out_ascii("no boot services\r\n"); for (;;) { } }

    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_ALLOCATE_PAGES allocp = (EFI_ALLOCATE_PAGES)bs->AllocatePages;
    EFI_STALL stall = (EFI_STALL)bs->Stall;
    EFI_STATUS s;

    UINT64 base = 0;
    if (!find_partition("rawdump", &base)) {
        out_ascii("rawdump not found\r\n"); for (;;) { stall(1000000); }
    }
    out_ascii("rawdump lba "); out_hex(base); out_ascii(" bs "); out_hex(gBlockSize); out_ascii("\r\n");

    // payload (64 MiB) at rawdump + 0
    UINT64 payload_addr = 0;
    s = allocp(0, EFI_LOADER_DATA, (UINTN)(PAYLOAD_MAX / 4096), &payload_addr);
    out_ascii("payload alloc "); out_hex(s); out_ascii(" @ "); out_hex(payload_addr); out_ascii("\r\n");
    if (s || !payload_addr) { for (;;) { stall(1000000); } }

    UINT64 done = 0;
    while (done < PAYLOAD_MAX) {
        UINT64 chunk = CHUNK;
        if (chunk > PAYLOAD_MAX - done) chunk = PAYLOAD_MAX - done;
        UINT64 lba = base + done / gBlockSize;
        if (gIo->ReadBlocks(gIo, gIo->Media->MediaId, lba, (UINTN)chunk, (UINT8 *)payload_addr + done)) break;
        done += chunk;
    }
    out_ascii("payload bytes "); out_hex(done); out_ascii("\r\n");

    // firmware volume (4 MiB) at rawdump + 64 MiB
    UINT64 fv_addr = 0;
    s = allocp(0, EFI_LOADER_DATA, (UINTN)(FD_MAX / 4096), &fv_addr);
    out_ascii("fv alloc "); out_hex(s); out_ascii(" @ "); out_hex(fv_addr); out_ascii("\r\n");
    if (s || !fv_addr) { for (;;) { stall(1000000); } }

    if (gIo->ReadBlocks(gIo, gIo->Media->MediaId, base + PAYLOAD_MAX / gBlockSize, (UINTN)FD_MAX, (void *)fv_addr)) {
        out_ascii("fv read failed\r\n"); for (;;) { stall(1000000); }
    }
    out_ascii("fv first "); out_hex(*(UINT32 *)fv_addr); out_ascii(" fvh "); out_hex(*(UINT32 *)(fv_addr + 0x28)); out_ascii("\r\n");

    UINT64 fdt_size = 0;
    UINT8 *fdt = fdt_build(payload_addr, payload_addr + done, &fdt_size);
    out_ascii("fdt "); out_hex(fdt_size); out_ascii(" @ "); out_hex((UINT64)fdt); out_ascii("\r\n");

    // copy the firmware volume to its link address
    UINT64 *src = (UINT64 *)fv_addr;
    UINT64 *dst = (UINT64 *)FD_BASE;
    for (UINTN k = 0; k < FD_MAX / 8; k++) dst[k] = src[k];
    sync_caches(FD_BASE, FD_MAX);
    out_ascii("copy first "); out_hex(*(UINT32 *)FD_BASE); out_ascii(" fvh "); out_hex(*(UINT32 *)(FD_BASE + 0x28)); out_ascii("\r\n");

    out_ascii("el "); out_hex(current_el()); out_ascii(" sctlr "); out_hex(read_sctlr()); out_ascii("\r\n");

    for (int i = 0; i < 400000; i++) __asm__ volatile("" ::: "memory");   // let the console flush
    out_ascii("mmu off + jump\r\n");
    for (int i = 0; i < 200000; i++) __asm__ volatile("" ::: "memory");
    disable_mmu_caches();
    jump_to_fd(FD_BASE, (UINT64)fdt);
}
