/*
 * SWEET internal UFS root device.
 *
 * The Qualcomm UFS host (UFSHCI 2.1) is left enabled with the link up by the
 * bootloader, which loaded our boot image from it. This driver reuses that
 * state: it points the transfer request list at its own memory (verifying the
 * host took it before ringing any doorbell) and issues polled SCSI commands,
 * one at a time, to LUN 0. It finds one GPT partition by name (boot-arg
 * ufsroot=, default "userdata") and exposes it as the BSD disk ufsroot0
 * (rufsroot0), with 512-byte logical sectors over the device's 4 KiB blocks.
 * Root on it with rd=ufsroot0.
 *
 * All DMA goes through one physically contiguous allocation (descriptors and
 * a 1 MiB bounce buffer); the host is not cache coherent, so caches are
 * cleaned/invalidated around each command.
 *
 * Writes are refused unless boot-arg ufsrw=1 is set. Every write is checked
 * against the partition bounds again right before it is issued, so nothing
 * outside the named partition can be written. The root itself is limited to
 * the pinfile extent table in /chosen/fuxi-root-extents.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <libkern/libkern.h>

extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);
/* osfmk/arm/machine_routines.h, not exported to the BSD layer */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define REG_CAP         0x00
#define REG_IS          0x20
#define REG_IE          0x24
#define REG_HCS         0x30
#define REG_HCE         0x34
#define REG_UTRLBA      0x50
#define REG_UTRLBAU     0x54
#define REG_UTRLDBR     0x58
#define REG_UTRLCLR     0x5c
#define REG_UTRLRSR     0x60
#define REG_UICCMD      0x90

#define HCS_DP          (1u << 0)
#define HCS_UTRLRDY     (1u << 1)
#define HCS_UPMCRS(hcs) (((hcs) >> 8) & 7u)
#define CAP_64AS        (1u << 24)              /* 64-bit DMA addressing */
#define IS_UCCS         (1u << 10)
#define IS_UHXS         (1u << 5)

#define UFS_SMMU_PA     0x15000000u             /* apps-smmu@15000000 */
#define UFS_SMMU_SIZE   0x1000u
#define SMMU_SCR0       0x0000u
#define SMMU_IDR0       0x0020u
#define SMMU_IDR1       0x0024u
#define SMMU_SMR(n)     (0x0800u + 4u * (n))
#define SMMU_S2CR(n)    (0x0c00u + 4u * (n))
#define SMMU_SMR_VALID  (1u << 31)
#define SMMU_S2CR_BYPASS 1u
#define UFS_STREAM_ID   0x60u

#define UFS_BLOCK       4096u
#define BOUNCE_SIZE     (1u << 20)
#define PRDT_CHUNK      (256u << 10)            /* PRDT byte count field: 18 bits */
#define PRDT_MAX        (BOUNCE_SIZE / PRDT_CHUNK)
#define CMD_TIMEOUT_NS  (5ull * NSEC_PER_SEC)

struct utrd {
	uint32_t dw0, dw1, dw2, dw3;
	uint32_t ucdba, ucdbau;
	uint32_t resp;          /* [31:16] response UPIU offset, [15:0] length, DWORDs */
	uint32_t prdt;          /* [31:16] PRDT offset (DWORDs), [15:0] entries */
};

struct ucd {
	uint8_t cmd[512];
	uint8_t resp[512];
	struct {
		uint32_t addr_lo, addr_hi, rsvd, dbc;
	} prdt[PRDT_MAX];
};

/* Layout of the contiguous DMA allocation. */
#define DMA_UTRL        0x0000                  /* 1 KiB aligned */
#define DMA_UCD         0x1000                  /* 128 byte aligned */
#define DMA_BOUNCE      0x4000
#define DMA_SIZE        (DMA_BOUNCE + BOUNCE_SIZE)

static struct {
	vm_offset_t regs;
	vm_offset_t dma;                        /* kernel VA of the DMA allocation */
	uint64_t dma_pa;
	uint8_t lun;
	uint8_t tag;
	uint64_t first_block, nblocks;          /* partition, in 4 KiB device blocks */
	uint32_t secsize;                       /* logical sector size for the BSD layer */
	bool writable;                          /* boot-arg ufsrw=1 */
	int bmajor, cmajor;
	dev_t bdev;
	bool ready;
} su;

/*
 * data-a-live / pinfile contract: /chosen/fuxi-root-extents contains u64
 * triples {file block, userdata block, block count}, all in 4 KiB units.
 * Only blocks covered by this table are exposed as ufsroot0 and writable.
 */
#define FX_MAX_EXTENTS 16384
static struct {
	uint64_t file, part, count;
} fx_ext[FX_MAX_EXTENTS];
static uint16_t fx_by_part[FX_MAX_EXTENTS];
static unsigned fx_next;
static bool fx_root;
static uint64_t fx_part_first, fx_part_blocks;

static LCK_GRP_DECLARE(sweet_ufs_grp, "sweet_ufs");
static LCK_MTX_DECLARE(sweet_ufs_lock, &sweet_ufs_grp);

static inline uint32_t
ufs_rd(uint32_t off)
{
	return *(volatile uint32_t *)(su.regs + off);
}

static inline void
ufs_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(su.regs + off) = v;
}

static void
dma_sync(uint32_t off, uint32_t len)
{
	flush_dcache64((addr64_t)(su.dma_pa + off), len, TRUE);
}

static bool
fx_map(uint64_t file_block, uint64_t *device_lba, uint64_t *run)
{
	unsigned lo = 0, hi = fx_next;
	while (lo < hi) {
		unsigned mid = lo + (hi - lo) / 2;
		if (file_block < fx_ext[mid].file) {
			hi = mid;
		} else if (file_block - fx_ext[mid].file >= fx_ext[mid].count) {
			lo = mid + 1;
		} else {
			uint64_t offset = file_block - fx_ext[mid].file;
			*device_lba = fx_part_first + fx_ext[mid].part + offset;
			*run = fx_ext[mid].count - offset;
			return true;
		}
	}
	return false;
}

static bool
fx_write_allowed(uint64_t lba, uint64_t count)
{
	if (!fx_root || count == 0 || lba < fx_part_first) {
		return false;
	}
	uint64_t relative = lba - fx_part_first;
	unsigned lo = 0, hi = fx_next;
	while (lo < hi) {
		unsigned mid = lo + (hi - lo) / 2;
		if (fx_ext[fx_by_part[mid]].part <= relative) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (lo == 0) {
		return false;
	}
	unsigned selected = fx_by_part[lo - 1];
	uint64_t first = fx_ext[selected].part;
	uint64_t length = fx_ext[selected].count;
	return relative - first < length && count <= length - (relative - first);
}

static int
fx_load_extents(void)
{
	DTEntry chosen;
	uint64_t const *extents = NULL, *blocks = NULL;
	unsigned int size = 0, blocks_size = 0;
	if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
	    SecureDTGetProperty(chosen, "fuxi-root-extents",
	    (void const **)&extents, &size) != kSuccess ||
	    SecureDTGetProperty(chosen, "fuxi-root-blocks",
	    (void const **)&blocks, &blocks_size) != kSuccess ||
	    blocks_size != sizeof(*blocks) || size == 0 ||
	    size % (3 * sizeof(uint64_t)) ||
	    size / (3 * sizeof(uint64_t)) > FX_MAX_EXTENTS) {
		printf("SWEET UFS: invalid /chosen/fuxi-root-extents\n");
		return ENXIO;
	}

	unsigned count = size / (3 * sizeof(uint64_t));
	uint64_t want = *blocks, logical = 0;
	for (unsigned i = 0; i < count && logical < want; i++) {
		uint64_t file = extents[3 * i];
		uint64_t part = extents[3 * i + 1];
		uint64_t length = extents[3 * i + 2];
		if (file != logical || length == 0 || part >= fx_part_blocks ||
		    length > fx_part_blocks - part) {
			printf("SWEET UFS: bad extent %u: file=%llu part=%llu blocks=%llu\n",
			    i, file, part, length);
			fx_next = 0;
			return EINVAL;
		}
		length = MIN(length, want - logical);
		fx_ext[fx_next].file = file;
		fx_ext[fx_next].part = part;
		fx_ext[fx_next].count = length;
		fx_next++;
		logical += length;
	}
	if (logical != want) {
		printf("SWEET UFS: extents cover %llu of %llu blocks\n", logical, want);
		fx_next = 0;
		return EINVAL;
	}

	for (unsigned i = 0; i < fx_next; i++) {
		fx_by_part[i] = (uint16_t)i;
	}
	for (unsigned gap = fx_next / 2; gap > 0; gap /= 2) {
		for (unsigned i = gap; i < fx_next; i++) {
			uint16_t value = fx_by_part[i];
			unsigned j = i;
			while (j >= gap &&
			    fx_ext[fx_by_part[j - gap]].part > fx_ext[value].part) {
				fx_by_part[j] = fx_by_part[j - gap];
				j -= gap;
			}
			fx_by_part[j] = value;
		}
	}
	for (unsigned i = 1; i < fx_next; i++) {
		unsigned a = fx_by_part[i - 1], b = fx_by_part[i];
		if (fx_ext[a].part + fx_ext[a].count > fx_ext[b].part) {
			printf("SWEET UFS: output extents %u and %u overlap\n", a, b);
			fx_next = 0;
			return EINVAL;
		}
	}
	fx_root = true;
	printf("SWEET UFS: root file has %u extents, %llu blocks (%llu MiB)\n",
	    fx_next, want, want * UFS_BLOCK >> 20);
	return 0;
}

/*
 * One SCSI command in slot 0, data (if any) to/from the bounce buffer
 * (to the device when write is set). Returns 0 or an errno.
 */
static int
ufs_scsi(const uint8_t *cdb, unsigned cdb_len, uint32_t len, bool write)
{
	struct utrd *utrd = (struct utrd *)(su.dma + DMA_UTRL);
	struct ucd *ucd = (struct ucd *)(su.dma + DMA_UCD);
	uint64_t ucd_pa = su.dma_pa + DMA_UCD, bounce_pa = su.dma_pa + DMA_BOUNCE;

	assert(len <= BOUNCE_SIZE);
	bzero(utrd, sizeof(*utrd));
	bzero(ucd, sizeof(*ucd));

	ucd->cmd[0] = 0x01;                     /* COMMAND UPIU */
	ucd->cmd[1] = len ? (write ? 0x20 : 0x40) : 0x00;      /* W: data out, R: data in */
	ucd->cmd[2] = su.lun;
	ucd->cmd[3] = ++su.tag;
	ucd->cmd[12] = (uint8_t)(len >> 24);
	ucd->cmd[13] = (uint8_t)(len >> 16);
	ucd->cmd[14] = (uint8_t)(len >> 8);
	ucd->cmd[15] = (uint8_t)len;
	memcpy(&ucd->cmd[16], cdb, cdb_len);

	/* DD: 1 = memory to device, 2 = device to memory. */
	utrd->dw0 = (1u << 28) | ((len ? (write ? 1u : 2u) : 0u) << 25) | (1u << 24);
	utrd->dw2 = 0x0f;                       /* OCS invalid until updated */
	utrd->ucdba = (uint32_t)ucd_pa;
	utrd->ucdbau = (uint32_t)(ucd_pa >> 32);
	utrd->resp = (512u / 4) << 16 | (512u / 4);
	if (len) {
		uint32_t n = 0;
		for (uint32_t done = 0; done < len; done += PRDT_CHUNK, n++) {
			uint32_t chunk = MIN(PRDT_CHUNK, len - done);
			ucd->prdt[n].addr_lo = (uint32_t)(bounce_pa + done);
			ucd->prdt[n].addr_hi = (uint32_t)((bounce_pa + done) >> 32);
			ucd->prdt[n].dbc = chunk - 1;
		}
		utrd->prdt = (1024u / 4) << 16 | n;
	}

	dma_sync(DMA_UTRL, sizeof(*utrd));
	dma_sync(DMA_UCD, sizeof(*ucd));
	if (len) {
		dma_sync(DMA_BOUNCE, len);
	}
	__builtin_arm_dsb(0xf);

	ufs_wr(REG_UTRLDBR, 1);
	uint64_t deadline;
	nanoseconds_to_absolutetime(CMD_TIMEOUT_NS, &deadline);
	deadline += mach_absolute_time();
	while (ufs_rd(REG_UTRLDBR) & 1) {
		if (mach_absolute_time() > deadline) {
			ufs_wr(REG_UTRLCLR, ~1u);
			printf("SWEET UFS: command 0x%02x timed out\n", cdb[0]);
			return ETIMEDOUT;
		}
		delay(1);
	}
	__builtin_arm_dsb(0xf);
	ufs_wr(REG_IS, ufs_rd(REG_IS));         /* acknowledge completion status */

	dma_sync(DMA_UTRL, sizeof(*utrd));
	dma_sync(DMA_UCD, sizeof(*ucd));
	if (len) {
		dma_sync(DMA_BOUNCE, len);
	}

	uint32_t ocs = utrd->dw2 & 0xff;
	if (ocs || ucd->resp[0] != 0x21 || ucd->resp[6] || ucd->resp[7]) {
		printf("SWEET UFS: command 0x%02x failed: ocs 0x%x upiu 0x%x response 0x%x status 0x%x\n",
		    cdb[0], ocs, ucd->resp[0], ucd->resp[6], ucd->resp[7]);
		return EIO;
	}
	return 0;
}

/*
 * The stock NX741J bootloader enables apps-smmu and gives UFS stream 0x60 a
 * translation entry. data-a-live programs the host with physical addresses,
 * so the same stream must be a bypass context. Find the matching SMR instead
 * of assuming that its index equals the stream ID.
 */
static int
ufs_smmu_bypass(void)
{
	volatile uint32_t *smmu = (volatile uint32_t *)ml_io_map(UFS_SMMU_PA, UFS_SMMU_SIZE);
	if (smmu == NULL) {
		printf("SWEET UFS: cannot map apps-smmu\n");
		return ENXIO;
	}
	uint32_t idr0 = smmu[SMMU_IDR0 / 4];
	uint32_t idr1 = smmu[SMMU_IDR1 / 4];
	unsigned groups = idr0 & 0xff;
	if (groups == 0 || idr0 == 0xffffffffu) {
		printf("SWEET UFS: apps-smmu IDR0=%08x is not usable\n", idr0);
		return ENXIO;
	}

	int selected = -1, free = -1;
	for (unsigned i = 0; i < groups; i++) {
		uint32_t smr = smmu[SMMU_SMR(i) / 4];
		if (!(smr & SMMU_SMR_VALID)) {
			if (free < 0) {
				free = (int)i;
			}
			continue;
		}
		uint32_t id = smr & 0x7fff;
		uint32_t mask = (smr >> 16) & 0x7fff;
		if (((id ^ UFS_STREAM_ID) & ~mask) == 0) {
			selected = (int)i;
			break;
		}
	}
	if (selected < 0) {
		selected = free;
		if (selected < 0) {
			printf("SWEET UFS: apps-smmu has no free SMR for stream 0x%x\n",
			    UFS_STREAM_ID);
			return ENXIO;
		}
		smmu[SMMU_SMR(selected) / 4] = SMMU_SMR_VALID | UFS_STREAM_ID;
	}
	uint32_t before = smmu[SMMU_S2CR(selected) / 4];
	smmu[SMMU_S2CR(selected) / 4] = SMMU_S2CR_BYPASS;
	uint32_t scr0 = smmu[SMMU_SCR0 / 4];
	if (!(scr0 & 1u)) {
		smmu[SMMU_SCR0 / 4] = scr0 | 1u;
	}
	__builtin_arm_dsb(0xf);
	uint32_t after = smmu[SMMU_S2CR(selected) / 4];
	printf("SWEET UFS: SMMU IDR0=%08x IDR1=%08x stream 0x%x SMR%u S2CR %08x -> %08x\n",
	    idr0, idr1, UFS_STREAM_ID, selected, before, after);
	return after == SMMU_S2CR_BYPASS ? 0 : EIO;
}

static int
ufs_hibernate_exit(void)
{
	if (!(ufs_rd(REG_HCS) & (1u << 3))) {
		return EBUSY;
	}
	ufs_wr(REG_IS, IS_UCCS | IS_UHXS);
	ufs_wr(REG_UICCMD + 4, 0);
	ufs_wr(REG_UICCMD + 8, 0);
	ufs_wr(REG_UICCMD + 12, 0);
	ufs_wr(REG_UICCMD, 0x18);                 /* DME_HIBERNATE_EXIT */
	for (int i = 0; i < 100000 && !(ufs_rd(REG_IS) & IS_UCCS); i++) {
		delay(1);
	}
	if (!(ufs_rd(REG_IS) & IS_UCCS)) {
		return ETIMEDOUT;
	}
	uint32_t result = ufs_rd(REG_UICCMD + 8) & 0xff;
	ufs_wr(REG_IS, IS_UCCS);
	if (result) {
		return (int)(0x100 | result);
	}
	for (int i = 0; i < 500000 && !(ufs_rd(REG_IS) & IS_UHXS); i++) {
		delay(1);
	}
	if (!(ufs_rd(REG_IS) & IS_UHXS)) {
		return (int)(0x300 | ETIMEDOUT);
	}
	ufs_wr(REG_IS, IS_UHXS);
	uint32_t power = HCS_UPMCRS(ufs_rd(REG_HCS));
	return power == 1 ? 0 : (int)(0x200 | power);
}

static int
ufs_device_active(void)
{
	static const uint8_t active[6] = { 0x1b, 0, 0, 0, 0x10, 0 };
	su.lun = 0xd0;
	int error = EIO;
	for (int i = 0; i < 3; i++) {
		error = ufs_scsi(active, sizeof(active), 0, false);
		if (!error) {
			break;
		}
	}
	su.lun = 0;
	printf("SWEET UFS: device START STOP UNIT ACTIVE: %d\n", error);
	return error;
}

/* READ(10) of count device blocks at lba into the bounce buffer. */
static int
ufs_read_blocks(uint64_t lba, uint32_t count)
{
	if (lba > UINT32_MAX || count > BOUNCE_SIZE / UFS_BLOCK) {
		return EINVAL;
	}
	uint8_t cdb[10] = {
		0x28, 0,
		(uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
		0, (uint8_t)(count >> 8), (uint8_t)count, 0
	};
	return ufs_scsi(cdb, sizeof(cdb), count * UFS_BLOCK, false);
}

/*
 * WRITE(10) of count device blocks from the bounce buffer to lba. The range
 * must lie inside the partition: this is the only place data goes out.
 */
static int
ufs_write_blocks(uint64_t lba, uint32_t count)
{
	if (!su.writable || !fx_write_allowed(lba, count) ||
	    count > BOUNCE_SIZE / UFS_BLOCK || lba + count - 1 > UINT32_MAX) {
		printf("SWEET UFS: refusing write of %u blocks at 0x%llx\n", count, lba);
		return EPERM;
	}
	uint8_t cdb[10] = {
		0x2a, 0,
		(uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
		0, (uint8_t)(count >> 8), (uint8_t)count, 0
	};
	return ufs_scsi(cdb, sizeof(cdb), count * UFS_BLOCK, true);
}

static int
ufs_sync_cache(void)
{
	static const uint8_t cdb[10] = { 0x35 };       /* SYNCHRONIZE CACHE(10), whole LUN */
	return ufs_scsi(cdb, sizeof(cdb), 0, false);
}

static int
ufs_find_partition(const char *want)
{
	const uint8_t *b = (const uint8_t *)(su.dma + DMA_BOUNCE);
	int error = ufs_read_blocks(1, 1);
	if (error) {
		return error;
	}
	if (memcmp(b, "EFI PART", 8) != 0) {
		printf("SWEET UFS: LUN %u has no GPT\n", su.lun);
		return ENXIO;
	}
	uint64_t entry_lba;
	uint32_t nent, esz;
	memcpy(&entry_lba, b + 72, 8);
	memcpy(&nent, b + 80, 4);
	memcpy(&esz, b + 84, 4);
	if (esz != 128 || nent == 0 || nent * esz > BOUNCE_SIZE) {
		printf("SWEET UFS: unsupported GPT entries %u x %u\n", nent, esz);
		return ENXIO;
	}
	error = ufs_read_blocks(entry_lba, (nent * esz + UFS_BLOCK - 1) / UFS_BLOCK);
	if (error) {
		return error;
	}
	for (uint32_t i = 0; i < nent; i++) {
		const uint8_t *e = b + i * esz;
		char name[37];
		unsigned c;
		for (c = 0; c < 36; c++) {
			uint16_t ch = (uint16_t)(e[56 + 2 * c] | e[57 + 2 * c] << 8);
			if (ch == 0 || ch > 0x7f) {
				break;
			}
			name[c] = (char)ch;
		}
		name[c] = 0;
		if (strcmp(name, want) != 0) {
			continue;
		}
		uint64_t first, last;
		memcpy(&first, e + 32, 8);
		memcpy(&last, e + 40, 8);
		if (last < first) {
			return ENXIO;
		}
		su.first_block = first;
		su.nblocks = last - first + 1;
		return 0;
	}
	printf("SWEET UFS: no GPT partition named \"%s\"\n", want);
	return ENOENT;
}

static int
ufs_take_over_host(void)
{
	uint32_t hce = ufs_rd(REG_HCE), hcs = ufs_rd(REG_HCS);
	if (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {
		printf("SWEET UFS: host not ready (hce 0x%x hcs 0x%x)\n", hce, hcs);
		return ENXIO;
	}
	if (ufs_rd(REG_UTRLDBR)) {
		printf("SWEET UFS: requests outstanding from the bootloader\n");
		return EBUSY;
	}
	/* With RAM above 4 GiB (mkadt.py --highmem) the DMA memory can be there too. */
	if (((su.dma_pa + DMA_SIZE - 1) >> 32) && !(ufs_rd(REG_CAP) & CAP_64AS)) {
		printf("SWEET UFS: DMA memory at 0x%llx but the host has no 64-bit addressing\n",
		    su.dma_pa);
		return ENXIO;
	}
	ufs_wr(REG_IE, 0);                      /* polled operation */
	ufs_wr(REG_IS, ufs_rd(REG_IS));
	ufs_wr(REG_UTRLRSR, 0);
	uint64_t list = su.dma_pa + DMA_UTRL;
	ufs_wr(REG_UTRLBA, (uint32_t)list);
	ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
	ufs_wr(REG_UTRLRSR, 1);
	/* Never ring a doorbell unless the host uses our list. */
	if (ufs_rd(REG_UTRLBA) != (uint32_t)list || ufs_rd(REG_UTRLBAU) != (uint32_t)(list >> 32) ||
	    !(ufs_rd(REG_UTRLRSR) & 1)) {
		printf("SWEET UFS: host did not take our request list\n");
		return EIO;
	}
	return 0;
}

static int
sweet_ufs_open(__unused dev_t dev, int flags, __unused int devtype, __unused struct proc *p)
{
	if (!su.ready) {
		return ENXIO;
	}
	return ((flags & FWRITE) && !su.writable) ? EROFS : 0;
}

static int
sweet_ufs_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	return 0;
}

static void
sweet_ufs_strategy(buf_t bp)
{
	int error = 0;
	caddr_t vaddr = NULL;
	uint64_t off = (uint64_t)buf_blkno(bp) * su.secsize;
	uint32_t count = buf_count(bp);
	uint64_t part_bytes = su.nblocks * UFS_BLOCK;

	bool rd = buf_flags(bp) & B_READ;

	buf_setresid(bp, count);
	if (!rd && !su.writable) {
		error = EROFS;
		goto done;
	}
	if (off > part_bytes || count > part_bytes - off) {
		error = EINVAL;
		goto done;
	}
	if (count == 0) {
		goto done;
	}
	if (buf_map(bp, &vaddr)) {
		error = EIO;
		goto done;
	}

	lck_mtx_lock(&sweet_ufs_lock);
	for (uint32_t done = 0; done < count;) {
		uint64_t pos = off + done;
		uint64_t lba = pos / UFS_BLOCK;
		uint32_t skip = (uint32_t)(pos % UFS_BLOCK);
		uint32_t want = MIN(count - done, BOUNCE_SIZE - skip);
		uint32_t blocks = (skip + want + UFS_BLOCK - 1) / UFS_BLOCK;
		void *bounce = (void *)(su.dma + DMA_BOUNCE);
		uint64_t device_lba, run;
		if (fx_root) {
			if (!fx_map(lba, &device_lba, &run)) {
				error = EIO;
				break;
			}
		} else {
			device_lba = su.first_block + lba;
			run = su.nblocks - lba;
		}
		if (blocks > run) {
			blocks = (uint32_t)run;
			want = blocks * UFS_BLOCK - skip;
		}
		if (rd) {
			error = ufs_read_blocks(device_lba, blocks);
			if (!error) {
				memcpy(vaddr + done, (const char *)bounce + skip, want);
			}
		} else {
			/* Partial device blocks at either end: read, merge, write back. */
			if (skip || (want % UFS_BLOCK)) {
				error = ufs_read_blocks(device_lba, blocks);
			}
			if (!error) {
				memcpy((char *)bounce + skip, vaddr + done, want);
				error = ufs_write_blocks(device_lba, blocks);
			}
		}
		if (error) {
			break;
		}
		done += want;
		buf_setresid(bp, count - done);
	}
	lck_mtx_unlock(&sweet_ufs_lock);
	buf_unmap(bp);

done:
	if (error) {
		buf_seterror(bp, error);
	}
	buf_biodone(bp);
}

static int
sweet_ufs_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int flag,
    __unused struct proc *p, int is_char)
{
	uint32_t *f = (uint32_t *)data;
	uint64_t *o = (uint64_t *)data;

	switch (cmd) {
	case DKIOCGETBLOCKSIZE:
		*f = su.secsize;
		return 0;
	case DKIOCSETBLOCKSIZE:
		if (is_char) {
			return ENODEV;
		}
		if (*f < DEV_BSIZE || *f > UFS_BLOCK || (*f & (*f - 1))) {
			return EINVAL;
		}
		su.secsize = *f;
		return 0;
	case DKIOCGETPHYSICALBLOCKSIZE:
		*f = UFS_BLOCK;
		return 0;
	case DKIOCGETBLOCKCOUNT:
		*o = su.nblocks * UFS_BLOCK / su.secsize;
		return 0;
	case DKIOCISWRITABLE:
		*f = su.writable;
		return 0;
	case DKIOCSYNCHRONIZE:
	case DKIOCSYNCHRONIZECACHE: {
		if (!su.writable) {
			return 0;
		}
		lck_mtx_lock(&sweet_ufs_lock);
		int error = ufs_sync_cache();
		lck_mtx_unlock(&sweet_ufs_lock);
		return error;
	}
	case DKIOCGETMAXBLOCKCOUNTREAD:
		*o = BOUNCE_SIZE / su.secsize;
		return 0;
	case DKIOCGETMAXBLOCKCOUNTWRITE:
		*o = su.writable ? BOUNCE_SIZE / su.secsize : 0;
		return 0;
	case DKIOCGETMAXSEGMENTCOUNTREAD:
	case DKIOCGETMAXSEGMENTCOUNTWRITE:
		*o = 1;
		return 0;
	default:
		return ENOTTY;
	}
}

static int
sweet_ufs_bioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return sweet_ufs_ioctl(dev, cmd, data, flag, p, 0);
}

static int
sweet_ufs_cioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return sweet_ufs_ioctl(dev, cmd, data, flag, p, 1);
}

static int
sweet_ufs_psize(__unused dev_t dev)
{
	return su.ready ? (int)MIN(su.nblocks * UFS_BLOCK / su.secsize, INT_MAX) : -1;
}

static int
sweet_ufs_read(dev_t dev, struct uio *uio, __unused int ioflag)
{
	return physio(sweet_ufs_strategy, NULL, dev, B_READ, minphys, uio, (int)su.secsize);
}

static int
sweet_ufs_write(dev_t dev, struct uio *uio, __unused int ioflag)
{
	if (!su.writable) {
		return EROFS;
	}
	return physio(sweet_ufs_strategy, NULL, dev, B_WRITE, minphys, uio, (int)su.secsize);
}

static const struct bdevsw sweet_ufs_bdevsw = {
	.d_open     = sweet_ufs_open,
	.d_close    = sweet_ufs_close,
	.d_strategy = sweet_ufs_strategy,
	.d_ioctl    = sweet_ufs_bioctl,
	.d_dump     = eno_dump,
	.d_psize    = sweet_ufs_psize,
	.d_type     = D_DISK,
};

static const struct cdevsw sweet_ufs_cdevsw = {
	.d_open       = sweet_ufs_open,
	.d_close      = sweet_ufs_close,
	.d_read       = sweet_ufs_read,
	.d_write      = sweet_ufs_write,
	.d_ioctl      = sweet_ufs_cioctl,
	.d_stop       = eno_stop,
	.d_reset      = eno_reset,
	.d_ttys       = NULL,
	.d_select     = eno_select,
	.d_mmap       = eno_mmap,
	.d_strategy   = eno_strat,
	.d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc,
	.d_type       = D_DISK,
};

static int
sweet_ufs_init(void)
{
	DTEntry node;
	uintptr_t const *reg = NULL;
	unsigned int size = 0;
	char want[64] = "userdata";
	int error;

	if (SecureDTLookupEntry(NULL, "/sweet-ufs", &node) != kSuccess ||
	    SecureDTGetProperty(node, "reg", (void const **)&reg, &size) != kSuccess ||
	    size < 2 * sizeof(uintptr_t)) {
		printf("SWEET UFS: no /sweet-ufs node in the device tree\n");
		return ENXIO;
	}
	PE_parse_boot_argn("ufsroot", want, sizeof(want));
	uint32_t rw = 0;
	PE_parse_boot_argn("ufsrw", &rw, sizeof(rw));

	su.regs = ml_io_map(reg[0], reg[1]);
	if (kmem_alloc_contig(kernel_map, &su.dma, DMA_SIZE, PAGE_MASK, 0, 0,
	    KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("SWEET UFS: no contiguous DMA memory\n");
		return ENOMEM;
	}
	ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)su.dma);
	if (!pn) {
		return ENOMEM;
	}
	su.dma_pa = ptoa_64(pn);
	if ((su.dma_pa + DMA_SIZE - 1) >> 36) {
		printf("SWEET UFS: DMA at 0x%llx is outside the 36-bit apps-smmu range\n",
		    su.dma_pa);
		return ENXIO;
	}
	su.lun = 0;
	su.secsize = DEV_BSIZE;

	lck_mtx_lock(&sweet_ufs_lock);
	error = ufs_smmu_bypass();
	if (!error) {
		error = ufs_take_over_host();
	}
	if (!error) {
		uint32_t power = HCS_UPMCRS(ufs_rd(REG_HCS));
		if (power == 1) {
			printf("SWEET UFS: link already in PWR_LOCAL; skipping hibernate exit\n");
		} else {
			error = ufs_hibernate_exit();
			printf("SWEET UFS: hibernate exit: %d, HCS=%08x IS=%08x\n",
			    error, ufs_rd(REG_HCS), ufs_rd(REG_IS));
		}
	}
	if (!error) {
		error = ufs_device_active();
	}
	if (!error) {
		static const uint8_t tur[6] = { 0x00 };
		/* The first command after the bootloader may report a unit attention. */
		for (int i = 0; i < 3 && (error = ufs_scsi(tur, sizeof(tur), 0, false)); i++) {
		}
	}
	if (!error) {
		error = ufs_find_partition(want);
	}
	if (!error && strcmp(want, "userdata") == 0) {
		fx_part_first = su.first_block;
		fx_part_blocks = su.nblocks;
		error = fx_load_extents();
		if (!error) {
			su.nblocks = fx_next ? fx_ext[fx_next - 1].file +
			    fx_ext[fx_next - 1].count : 0;
		}
	}
	lck_mtx_unlock(&sweet_ufs_lock);
	if (error) {
		return error;
	}

	su.bmajor = bdevsw_add(-1, &sweet_ufs_bdevsw);
	if (su.bmajor < 0) {
		return ENXIO;
	}
	su.cmajor = cdevsw_add_with_bdev(-1, &sweet_ufs_cdevsw, su.bmajor);
	if (su.cmajor < 0) {
		return ENXIO;
	}
	su.bdev = makedev(su.bmajor, 0);
	devfs_make_node(su.bdev, DEVFS_BLOCK, UID_ROOT, GID_OPERATOR, 0640, "ufsroot0");
	devfs_make_node(makedev(su.cmajor, 0), DEVFS_CHAR, UID_ROOT, GID_OPERATOR, 0640, "rufsroot0");
	/* Every write is checked against the pinned root's extents. */
	su.writable = (rw != 0 && fx_root);
	su.ready = true;
	printf("SWEET UFS: \"%s\" on LUN %u: first block 0x%llx, %llu MiB, %s, DMA at 0x%llx\n",
	    want, su.lun, su.first_block, su.nblocks * UFS_BLOCK >> 20,
	    su.writable ? "read-write" : "read-only", su.dma_pa);
	return 0;
}

/* Root device for rd=ufsroot0 (IOFindBSDRoot). */
int sweet_ufs_rootdev(dev_t *dev);

int
sweet_ufs_rootdev(dev_t *dev)
{
	if (!su.ready) {
		int error = sweet_ufs_init();
		if (error) {
			return error;
		}
	}
	*dev = su.bdev;
	return 0;
}
