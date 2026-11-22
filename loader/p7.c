// UniXNU efisp loader - phase 7 (ARM64 PE32+).
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
// ---- boot progress record (rawdump + 79 MiB) ------------------------------
typedef struct { UINT32 Magic; UINT32 Step; UINT64 Status; UINT64 A; UINT64 B; UINT64 C; } BP_RECORD;
static UINT64 gRawBase;
static UINT32 gRawBs;
/* Unique to the MMU-safe P7 hand-off so a stale record cannot be mistaken for
 * a fresh boot. Keep 10/11 as the last firmware-side milestones. */
#define P7_PROGRESS_TAG  0x5037001120221130ULL

static void write_progress(UINT32 step, UINT64 status)
{
    static UINT8 rec[4096];
    BP_RECORD *r = (BP_RECORD *)rec;
    UINTN i;
    for (i = 0; i < sizeof(rec); i++) rec[i] = 0;
    r->Magic = 0x32444244;
    r->Step = step;
    r->Status = status;
    // Never fall back to an absolute LBA.  A zero base used to write this
    // record into the super partition instead of rawdump.
    if (gIo == 0 || gRawBs == 0 || gRawBase == 0) return;
    ((EFI_STATUS (*)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *))gIo->WriteBlocks)
        (gIo, gIo->Media->MediaId, gRawBase + (79ULL * 1024 * 1024) / gRawBs, 4096, rec);
}
// ---------------------------------------------------------------------------


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

// ---------------------------------------------------------------------------
// NX741J console log capture: the kernel keeps its whole console output in a
// static buffer that starts with the "NXLOG1" magic.  Scanning DRAM for that
// magic and dropping the payload into rawdump+82 MiB gives the host a log
// channel on a board with no UART.
/*
  Snapshot of the UFS host the bootloader left behind.  The firmware (Mu) drives
  UFS before XNU runs, so this is the only place where the ABL hand-off state can
  be read: compare it with what the kernel later sees to tell whether the firmware
  changed anything.  Written to rawdump LBA 4764488 as a 4 KiB record.
*/
static void dump_ufs_state(void)
{
    static UINT8 rec[4096] __attribute__((aligned(4096)));
    UINTN bs3 = gRawBs ? gRawBs : 4096;
    UINT64 hc = 0x1d84000ULL, mca = 0x1da0000ULL;
    UINT32 vals[16];
    UINTN i;
    for (i = 0; i < sizeof(rec); i++) rec[i] = 0;
    rec[0] = 'U'; rec[1] = 'F'; rec[2] = 'S'; rec[3] = 'S';
    rec[4] = 'T'; rec[5] = 'A'; rec[6] = 'T'; rec[7] = '1';
    vals[0]  = *(volatile UINT32 *)(hc + 0x00);   /* CAP */
    vals[1]  = *(volatile UINT32 *)(hc + 0x08);   /* VER */
    vals[2]  = *(volatile UINT32 *)(hc + 0x20);   /* IS */
    vals[3]  = *(volatile UINT32 *)(hc + 0x24);   /* IE */
    vals[4]  = *(volatile UINT32 *)(hc + 0x30);   /* HCS */
    vals[5]  = *(volatile UINT32 *)(hc + 0x34);   /* HCE */
    vals[6]  = *(volatile UINT32 *)(hc + 0x50);   /* UTRLBA */
    vals[7]  = *(volatile UINT32 *)(hc + 0x54);   /* UTRLBAU */
    vals[8]  = *(volatile UINT32 *)(hc + 0x58);   /* UTRLDBR */
    vals[9]  = *(volatile UINT32 *)(hc + 0x5c);   /* UTRLCLR */
    vals[10] = *(volatile UINT32 *)(hc + 0x60);   /* UTRLRSR */
    vals[11] = *(volatile UINT32 *)(hc + 0x300);  /* MEM_CFG */
    vals[12] = *(volatile UINT32 *)(hc + 0x380);  /* MCQ_CFG */
    vals[13] = *(volatile UINT32 *)(mca + 0x00);  /* MCA SQATTR */
    vals[14] = *(volatile UINT32 *)(mca + 0x20);  /* MCA CQATTR */
    vals[15] = 0xfeedface;
    for (i = 0; i < 16; i++) *(UINT32 *)(rec + 16 + i * 4) = vals[i];
    ((EFI_STATUS (*)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *))gIo->WriteBlocks)
        (gIo, gIo->Media->MediaId, 4764488ULL, bs3, rec);
    out_ascii("ufs snapshot "); out_hex(vals[6]); out_ascii("/"); out_hex(vals[4]); out_ascii("\r\n");
}

/* Read-only GIC snapshot used to validate the redistributor walk before DXE. */
static void dump_gic_state(void)
{
    static UINT8 rec[4096] __attribute__((aligned(4096)));
    UINTN bs3 = gRawBs ? gRawBs : 4096;
    UINT64 gicd = 0x17000000ULL;
    UINT64 gicr = 0x17080000ULL;
    UINT64 mpidr;
    UINT64 cpu_affinity;
    UINT32 match = 0xffffffffU;
    UINT32 count = 0;
    UINTN i;

    for (i = 0; i < sizeof(rec); i++) rec[i] = 0;
    rec[0] = 'G'; rec[1] = 'I'; rec[2] = 'C'; rec[3] = 'S';
    rec[4] = 'T'; rec[5] = 'A'; rec[6] = 'T'; rec[7] = '1';
    *(UINT32 *)(rec + 8) = *(volatile UINT32 *)(gicd + 0x0);
    *(UINT32 *)(rec + 12) = *(volatile UINT32 *)(gicd + 0x4);
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    cpu_affinity = (mpidr & 0xffffffULL) | ((mpidr & 0xff00000000ULL) >> 8);
    *(UINT64 *)(rec + 16) = mpidr;
    *(UINT64 *)(rec + 24) = cpu_affinity;

    for (i = 0; i < 16; i++) {
        UINT64 base = gicr + i * 0x40000ULL;
        UINT64 typer = *(volatile UINT64 *)(base + 0x8);
        UINT64 affinity = typer >> 32;
        UINT8 *entry = rec + 0x30 + i * 0x20;
        *(UINT64 *)(entry + 0) = base;
        *(UINT64 *)(entry + 8) = typer;
        *(UINT64 *)(entry + 16) = affinity;
        *(UINT32 *)(entry + 24) =
          ((typer & 0x2ULL) ? 1U : 0U) | ((typer & 0x10ULL) ? 2U : 0U);
        count++;
        if (affinity == cpu_affinity) {
            match = (UINT32)i;
        }
        if ((typer & 0x10ULL) != 0) {
            break;
        }
    }
    *(UINT32 *)(rec + 0x20) = match;
    *(UINT32 *)(rec + 0x24) = count;
    ((EFI_STATUS (*)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *))gIo->WriteBlocks)
        (gIo, gIo->Media->MediaId, 4764560ULL, bs3, rec);
    out_ascii("gic snapshot match "); out_hex(match); out_ascii(" frames "); out_hex(count); out_ascii("\r\n");
}

/* Persist the reserved-RAM markers written by StageProbeDxe. */
static void dump_ram_stage(void)
{
    static UINT8 rec[4096] __attribute__((aligned(4096)));
    UINTN bs3 = gRawBs ? gRawBs : 4096;
    UINT64 bases[4] = { 0xC6D00000ULL, 0xC6D01000ULL, 0xC6D02000ULL, 0xC6D03000ULL };
    UINTN i;
    UINTN page;

    for (page = 0; page < 4; page++) {
        volatile UINT64 *stage = (volatile UINT64 *)(UINTN)bases[page];
        for (i = 0; i < sizeof(rec); i++) rec[i] = 0;
        rec[0] = 'R'; rec[1] = 'A'; rec[2] = 'M'; rec[3] = 'S';
        rec[4] = 'T'; rec[5] = 'G'; rec[6] = '0'; rec[7] = '1';
        for (i = 0; i < 8; i++) {
            *(UINT64 *)(rec + 0x10 + i * 8) = stage[i];
        }
        ((EFI_STATUS (*)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *))gIo->WriteBlocks)
            (gIo, gIo->Media->MediaId, 4764561ULL + page, bs3, rec);
    }
}

/*
  Which descriptors describe the SMMU window?  The kernel cannot read past
  0x15001fff without an external abort, so the question is whether the firmware
  memory map describes the whole 1 MiB the device tree claims.  This walks the
  GCD only (no device access) and writes the answer to rawdump LBA 4764490.
*/
static void dump_smmu_gcd(void)
{
    static UINT8 rec[4096];
    UINTN bs3 = gRawBs ? gRawBs : 4096;
    EFI_BOOT_SERVICES *bsvc = gST->BootServices;
    UINTN size = 0, key = 0, descSize = 0, count = 0, i, n = 0;
    UINT32 descVer = 0;
    UINT8 *map = 0;
    for (i = 0; i < sizeof(rec); i++) rec[i] = 0;
    rec[0] = 'G'; rec[1] = 'C'; rec[2] = 'D'; rec[3] = 'S';
    rec[4] = 'M'; rec[5] = 'M'; rec[6] = 'U'; rec[7] = '1';
    ((EFI_STATUS (*)(UINTN *, void **, UINTN *, UINTN *, UINT32 *))bsvc->GetMemoryMap)
        (&size, 0, &key, &descSize, &descVer);
    size += 16 * descSize + 4096;
    if (!((EFI_STATUS (*)(UINTN, UINTN, void **))bsvc->AllocatePool)(1, size, (void **)&map) && map) {
        if (!((EFI_STATUS (*)(UINTN *, void *, UINTN *, UINTN *, UINT32 *))bsvc->GetMemoryMap)
                 (&size, map, &key, &descSize, &descVer)) {
            count = size / descSize;
        }
    }
    *(UINT32 *)(rec + 8) = (UINT32)count;
    for (i = 0; i < count && n < 8; i++) {
        UINT8 *d = map + i * descSize;
        UINT32 type = *(UINT32 *)d;
        UINT64 base = *(UINT64 *)(d + 8);
        UINT64 bytes = *(UINT64 *)(d + 24) * 4096ULL;
        if (base <= 0x15000000ULL && 0x15000000ULL < base + bytes) {
            UINT8 *e = rec + 16 + n * 32;
            *(UINT64 *)(e + 0) = base;
            *(UINT64 *)(e + 8) = bytes;
            *(UINT32 *)(e + 16) = type;
            *(UINT32 *)(e + 20) = (UINT32)i;
            n++;
        }
    }
    *(UINT32 *)(rec + 12) = (UINT32)n;
    ((EFI_STATUS (*)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *))gIo->WriteBlocks)
        (gIo, gIo->Media->MediaId, 4764490ULL, bs3, rec);
    out_ascii("smmu gcd "); out_hex((UINT64)n); out_ascii("\r\n");
}

static void capture_previous_log(void)
{
    volatile UINT64 *p;
    UINT64 found = 0;
    UINT32 foundLen = 0;
    UINT32 matches = 0;
    out_ascii("nxlog scan\r\n");
    /*
      DRAM keeps the buffers of earlier boots too, so every match is examined and
      the longest one wins: it is the most recent boot and the one that ran the
      console to its end (a panic leaves more text than an early hang).
    */
    EFI_BOOT_SERVICES *bsvc = gST->BootServices;
    UINTN size = 0, key = 0, descSize = 0;
    UINT32 descVer = 0;
    UINT8 *map = 0;
    UINTN count = 0;
    /*
      Only walk memory the firmware actually describes: a fixed range hit an
      unmapped hole once and wedged the loader, and the GCD map is authoritative.
      EfiLoaderCode..EfiConventionalMemory (1..7) covers the window DarwinBoot
      allocated, the firmware heap and free DRAM.
    */
    ((EFI_STATUS (*)(UINTN *, void **, UINTN *, UINTN *, UINT32 *))bsvc->GetMemoryMap)
        (&size, 0, &key, &descSize, &descVer);
    size += 16 * descSize + 4096;
    if (!((EFI_STATUS (*)(UINTN, UINTN, void **))bsvc->AllocatePool)(1, size, (void **)&map) && map) {
        if (!((EFI_STATUS (*)(UINTN *, void *, UINTN *, UINTN *, UINT32 *))bsvc->GetMemoryMap)
                 (&size, map, &key, &descSize, &descVer)) {
            count = size / descSize;
        }
    }
    out_ascii("nxlog ranges "); out_hex(count); out_ascii("\r\n");
    for (UINTN i = 0; i < count; i++) {
        UINT8 *d = map + i * descSize;
        UINT32 type = *(UINT32 *)d;
        UINT64 base = *(UINT64 *)(d + 8);
        UINT64 pages = *(UINT64 *)(d + 24);
        if (type < 1 || type > 7) { continue; }
        for (UINT64 a = base & ~0xFFFULL; a < base + pages * 4096ULL; a += 0x1000ULL) {
            p = (volatile UINT64 *)a;
            if (p[0] == 0x0031474F4C584EULL) {
                UINT32 probe = *(volatile UINT32 *)(a + 8);
                if (probe > 0 && probe < 0x1FFF0U) {
                    matches++;
                    if (probe > foundLen) { found = a; foundLen = probe; }
                }
            }
        }
    }
    {
        static UINT8 st[4096];
        UINT8 *q = st;
        for (UINTN z = 0; z < sizeof(st); z++) st[z] = 0;
        q[0] = 'N'; q[1] = 'X'; q[2] = 'L'; q[3] = 'G'; q[4] = 'S'; q[5] = 'T'; q[6] = '1';
        *(UINT64 *)(st + 8) = found;
        *(UINT32 *)(st + 24) = matches;
        if (found) {
            UINT32 l2 = *(volatile UINT32 *)(found + 8);
            *(UINT32 *)(st + 16) = l2;
            for (UINTN z = 0; z < 64; z++) st[32 + z] = *(UINT8 *)(found + 12 + z);
        }
        ((EFI_STATUS (*)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *))gIo->WriteBlocks)
            (gIo, gIo->Media->MediaId, 4764464ULL, 4096, st);   // absolute LBA, verified readable over 9008
    }
    if (!found) { out_ascii("nxlog none\r\n"); return; }
    UINT32 len = *(volatile UINT32 *)(found + 8);
    if (len > 0x1FFF0U) len = 0x1FFF0U;
    UINTN bs = gRawBs ? gRawBs : 4096;
    // Keep the log immediately after the status block: that LBA is proven writable.
    UINT64 lba = 4764465ULL;
    UINT8 *src = (UINT8 *)(found + 12);
    static __attribute__((aligned(4096))) UINT8 tmp[4096];
    for (UINT32 off = 0; off < len; off += (UINT32)bs) {
        UINTN chunk = (len - off > bs) ? bs : (UINTN)(len - off);
        /* BlockIo needs a block-aligned buffer, which found+12 is not. */
        for (UINTN z = 0; z < bs; z++) tmp[z] = (z < chunk) ? src[off + z] : 0;
        ((EFI_STATUS (*)(EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *))gIo->WriteBlocks)
            (gIo, gIo->Media->MediaId, lba + off / bs, bs, tmp);
    }
    out_ascii("nxlog captured "); out_hex(len); out_ascii("\r\n");
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    (void)image;
    gST = st;
    out_ascii("\r\n[UNIXNU] efisp loader phase 7\r\n");
    if (!st || !st->BootServices) { out_ascii("no boot services\r\n"); for (;;) { } }

    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_ALLOCATE_PAGES allocp = (EFI_ALLOCATE_PAGES)bs->AllocatePages;
    EFI_STALL stall = (EFI_STALL)bs->Stall;
    EFI_STATUS s;

    UINT64 base = 0;
    if (!find_partition("rawdump", &base)) {
        out_ascii("rawdump not found\r\n"); for (;;) { stall(1000000); }
    }
    gRawBase = base;
    gRawBs = gBlockSize;
    dump_ram_stage();
#ifndef P7_QEMU_TEST
    dump_ufs_state();
#endif
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
    if (done != PAYLOAD_MAX) {
        out_ascii("payload incomplete, returning to ABL\r\n");
        stall(2000000);
        return 1;
    }

    // firmware volume (4 MiB) at rawdump + 64 MiB
    UINT64 fv_addr = 0;
    s = allocp(0, EFI_LOADER_DATA, (UINTN)(FD_MAX / 4096), &fv_addr);
    out_ascii("fv alloc "); out_hex(s); out_ascii(" @ "); out_hex(fv_addr); out_ascii("\r\n");
    if (s || !fv_addr) { for (;;) { stall(1000000); } }

    if (gIo->ReadBlocks(gIo, gIo->Media->MediaId, base + PAYLOAD_MAX / gBlockSize, (UINTN)FD_MAX, (void *)fv_addr)) {
        out_ascii("fv read failed\r\n"); for (;;) { stall(1000000); }
    }
    {
        UINT32 first = *(UINT32 *)fv_addr;
        UINT32 fvh = *(UINT32 *)(fv_addr + 0x28);
        UINT64 fv_len = *(UINT64 *)(fv_addr + 0x20);
        out_ascii("fv first "); out_hex(first); out_ascii(" fvh "); out_hex(fvh);
        out_ascii(" len "); out_hex(fv_len); out_ascii("\r\n");
        // EFI_FIRMWARE_VOLUME_HEADER: signature "_FVH" at 0x28, plausible length.
        if (fvh != 0x4856465fU || fv_len < 0x1000ULL || fv_len > FD_MAX) {
            out_ascii("invalid firmware volume, returning to ABL\r\n");
            stall(2000000);
            return 1;
        }
    }

    UINT64 fdt_size = 0;
    UINT8 *fdt = fdt_build(payload_addr, payload_addr + done, &fdt_size);
    out_ascii("fdt "); out_hex(fdt_size); out_ascii(" @ "); out_hex((UINT64)fdt); out_ascii("\r\n");

    // Run every diagnostic that allocates pool memory before copying the FD.
    // The ABL map can still describe FD_BASE as conventional memory, so a pool
    // allocation after the copy may overwrite SEC/PEI before the first branch.
    write_progress(10, P7_PROGRESS_TAG);
#ifndef P7_QEMU_TEST
    dump_gic_state();
    dump_smmu_gcd();
    capture_previous_log();
#endif
    // Also record the hand-off before copying. Once the FD is at FD_BASE, do
    // not enter any storage driver: its internal DMA/pool buffers can overlap
    // the conventional-memory window ABL reports for the firmware volume.
    write_progress(11, P7_PROGRESS_TAG);
    for (int i = 0; i < 400000; i++) __asm__ volatile("" ::: "memory");   // let the console flush
    out_ascii("mask irq, own stack, mmu off\r\n");
    for (int i = 0; i < 200000; i++) __asm__ volatile("" ::: "memory");

    // copy the firmware volume to its link address
    UINT64 *src = (UINT64 *)fv_addr;
    UINT64 *dst = (UINT64 *)FD_BASE;
    for (UINTN k = 0; k < FD_MAX / 8; k++) dst[k] = src[k];
    sync_caches(FD_BASE, FD_MAX);
    out_ascii("copy first "); out_hex(*(UINT32 *)FD_BASE); out_ascii(" fvh "); out_hex(*(UINT32 *)(FD_BASE + 0x28)); out_ascii("\r\n");
    {
        UINT64 diff = 0;
        for (UINTN k = 0; k < FD_MAX / 8; k++) if (dst[k] != src[k]) diff++;
        out_ascii("copy diff "); out_hex(diff); out_ascii("\r\n");
        if (diff) {
            out_ascii("firmware copy mismatch, returning to ABL\r\n");
            stall(2000000);
            return 1;
        }
    }

    out_ascii("el "); out_hex(current_el()); out_ascii(" sctlr "); out_hex(read_sctlr()); out_ascii("\r\n");

    // Mask IRQ/FIQ/SError/debug first: with the MMU off a stray IRQ would vector through
    // the previous stage's VBAR and fault. Then switch to our own stack, disable the MMU
    // and caches, and branch into the firmware volume.
    static __attribute__((aligned(16))) UINT8 gStack[32768];
    UINT64 sp_top = (UINT64)&gStack[sizeof(gStack) - 16];
    __asm__ volatile("msr daifset, #0xf\n isb" ::: "memory");
    __asm__ volatile("mov sp, %0" :: "r"(sp_top) : "memory");
    for (int i = 0; i < 100000; i++) __asm__ volatile("" ::: "memory");
    disable_mmu_caches();
    jump_to_fd(FD_BASE, (UINT64)fdt);
}
