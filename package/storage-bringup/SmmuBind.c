// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Board binding for the ArmSmmu4K kit (qsmmu-v500 at 0x15000000).
//
// Stages, each gated so an untested block can never hang the boot:
//
//   build    stage-1 tables in a page-checked static pool (default on)
//   probe    map the registers, read ID0/ID1/ID2/SCR0/sACR/SFSR and dump the
//            live SMR/S2CR table, then record which slot and bank the
//            firmware already gave UFS stream 0x60 (smmu-probe=1)
//   attach   program that context bank and attach that slot (smmu-attach=1)
//
// Register offsets are the ones the kit itself uses in ArmSmmu4KStreams.c and
// ArmSmmu4KProgram.c: sCR0 0x0, sACR 0xC, ID0/1/2 0x20/0x24/0x28, SFSR 0x48,
// SMR[n] 0x800+4n, S2CR[n] 0xC00+4n, CBAR[n] 0x8000+4n, CBA2R[n] 0x8800+4n.
#include <stdint.h>

#include <arm/caches_internal.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>

#include "ArmSmmu4K.h"
#include "ArmSmmu4KStreams.h"
#include "ArmSmmu4KMmio.h"

#define BOARD_SMMU_BASE          UINT64_C(0x15000000)
#define BOARD_SMMU_BYTES         UINT64_C(0x100000)
#define BOARD_SMMU_POOL_PAGES    8u
#define BOARD_SMMU_PAGE_BYTES    4096u
#define BOARD_SMMU_UFS_SID       0x60u
#define BOARD_SMMU_POLL_BUDGET   1000u
#define BOARD_SMMU_MAX_SCAN      64u

#define REG_SCR0                 0x0u
#define REG_SACR                 0xCu
#define REG_ID0                  0x20u
#define REG_ID1                  0x24u
#define REG_ID2                  0x28u
#define REG_SFSR                 0x48u
#define REG_SMR(n)               (0x800u + 4u * (n))
#define REG_S2CR(n)              (0xC00u + 4u * (n))
#define REG_CBAR(n)              (0x8000u + 4u * (n))
#define REG_CBA2R(n)             (0x8800u + 4u * (n))

typedef struct {
    int mapped, bound, probed, built, attached;
    int probe_status, build_status, attach_status;
    uint32_t scr0, sacr, sfsr, id0, id1, id2;
    unsigned slots;
    unsigned ufs_slot, ufs_bank;
    int ufs_found;
    int ufs_bypass;
    uint64_t pool_physical;
    ArmSmmu4KStatus last;
} board_smmu_state_t;

static int board_smmu_gate(const char *argument);

static uint64_t board_smmu_tables[BOARD_SMMU_POOL_PAGES * 512] __attribute__((aligned(BOARD_SMMU_PAGE_BYTES)));
static ArmSmmu4KConfig board_smmu_config;
static ArmSmmu4KResult board_smmu_result;
static ArmSmmu4KMmio board_smmu_mmio;
static ArmSmmu4KIo board_smmu_io;
static board_smmu_state_t board_smmu;

void board_smmu_status_dump(void);

const board_smmu_state_t *board_smmu_status(void)
{
    return &board_smmu;
}

const ArmSmmu4KResult *board_smmu_last_result(void)
{
    return &board_smmu_result;
}

static uint32_t board_smmu_read32(unsigned offset)
{
    uint32_t value = 0xffffffffu;

    if (board_smmu_io.read32(board_smmu_io.cookie, offset, &value) != 0) {
        return 0xffffffffu;
    }
    return value;
}

static void board_smmu_write32(unsigned offset, uint32_t value)
{
    *(volatile uint32_t *)(board_smmu_mmio.mapping + offset) = value;
    __sync_synchronize();
}

int board_smmu_probe(void);

/* docs/SMMU-BRINGUP.md step 1/2: firmware left SID 0x60 unrouted and the SMMU
 * globally bypassed (SCTLR.M = 0).  NativeUfs plans its own SMMU context and
 * refuses to start without a route, so route the stream as bypass here, before
 * IOKit probes storage.  Bypass keeps UFS DMA physical, which is how the
 * earlier working boot ran. */
int board_smmu_prepare_ufs(void)
{
    unsigned slot;

    if (board_smmu_probe() != 0) {
        return -1;
    }
    if (board_smmu.ufs_found && !board_smmu.ufs_bypass) {
        return 0;
    }
    slot = board_smmu.ufs_slot;
    if (slot == ~0u) {
        return -2;
    }
    board_smmu_write32(REG_SMR(slot), (1u << 31) | BOARD_SMMU_UFS_SID);
    board_smmu_write32(REG_S2CR(slot), 1u); /* type 1 = bypass */
    board_smmu_write32(REG_SCR0, board_smmu.scr0 | 1u); /* SCTLR.M = 1 */
    __sync_synchronize();
    board_smmu.ufs_bank = 0;
    board_smmu.ufs_found = 1;
    board_smmu.ufs_bypass = 1;
    printf("SMMU: routed sid %03x as bypass at slot %u (scr0 %08x -> %08x)\n",
           BOARD_SMMU_UFS_SID, slot, board_smmu.scr0, board_smmu.scr0 | 1u);
    return 0;
}

static int board_smmu_map(void)
{
    vm_offset_t mapping;

    if (board_smmu.mapped) {
        return 0;
    }
    mapping = ml_io_map((vm_offset_t)BOARD_SMMU_BASE, (vm_size_t)BOARD_SMMU_BYTES);
    if (!mapping) {
        return -1;
    }
    board_smmu_mmio.mapping = (volatile unsigned char *)mapping;
    board_smmu_mmio.bytes = (size_t)BOARD_SMMU_BYTES;
    board_smmu.last = ArmSmmu4KBindMmio(&board_smmu_mmio, &board_smmu_io);
    board_smmu.mapped = 1;
    board_smmu.bound = board_smmu.last == ArmSmmu4KSuccess;
    return board_smmu.bound ? 0 : -1;
}

/* Decode the live SMR/S2CR table and remember where SID 0x60 already goes.
 * The firmware programs that route before we run, so reading it is how the
 * bank and slot are learned without the vendor's table. */
static void board_smmu_scan(void)
{
    int printed = 0;
    unsigned free_slot = ~0u;
    unsigned exids = (board_smmu.id0 >> 8) & 1u;
    unsigned mask_bits = exids ? 0xffffu : 0x7fffu;

    board_smmu.slots = board_smmu.id0 & 0xffu;
    if (board_smmu.slots > BOARD_SMMU_MAX_SCAN) {
        board_smmu.slots = BOARD_SMMU_MAX_SCAN;
    }
    printf("SMMU: scr0 %08x id0 %08x id1 %08x id2 %08x sacr %08x sfsr %08x slots %u\n",
           board_smmu.scr0, board_smmu.id0, board_smmu.id1, board_smmu.id2,
           board_smmu.sacr, board_smmu.sfsr, board_smmu.slots);

    for (unsigned slot = 0; slot < board_smmu.slots; ++slot) {
        uint32_t smr = board_smmu_read32(REG_SMR(slot));
        uint32_t s2cr = board_smmu_read32(REG_S2CR(slot));
        unsigned valid = exids ? ((s2cr >> 10) & 1u) : ((smr >> 31) & 1u);
        unsigned type = (s2cr >> 16) & 3u;
        unsigned sid = smr & 0xffffu;
        unsigned mask = (smr >> 16) & mask_bits;
        unsigned bank = s2cr & 0xffu;

        if (valid && printed < 12) {
            printf("SMMU: slot %u smr %08x s2cr %08x sid %03x mask %03x type %u bank %u\n",
                   slot, smr, s2cr, sid, mask, type, bank);
            ++printed;
        }
        if (!valid && free_slot == ~0u) {
            free_slot = slot;
        }
        if (sid == BOARD_SMMU_UFS_SID && type == 0 && valid) {
            board_smmu.ufs_slot = slot;
            board_smmu.ufs_bank = bank;
            board_smmu.ufs_found = 1;
        }
    }
    if (!board_smmu.ufs_found && (board_smmu.scr0 & 1u) == 0 && free_slot != ~0u) {
        /*
         * docs/SMMU-BRINGUP.md step 0: firmware did not route SID 0x60.  With
         * SCTLR.M clear the SMMU is globally bypassed, so UFS DMA is physical
         * already (this is how the previous boot reached userland).  Report the
         * free slot with the bypass flag instead of leaving slot = -1.
         */
        board_smmu.ufs_slot = free_slot;
        board_smmu.ufs_bank = 0;
        board_smmu.ufs_found = 1;
        board_smmu.ufs_bypass = 1;
        printf("SMMU: ufs sid %03x not routed; SMMU off, reporting global bypass slot %u\n",
               BOARD_SMMU_UFS_SID, free_slot);
    }

    if (board_smmu.ufs_found) {
        uint32_t cbar = board_smmu_read32(REG_CBAR(board_smmu.ufs_bank));
        printf("SMMU: ufs sid %03x slot %u bank %u cbar %08x\n",
               BOARD_SMMU_UFS_SID, board_smmu.ufs_slot, board_smmu.ufs_bank, cbar);
    } else {
        printf("SMMU: ufs sid %03x is not routed yet (fault or unset)\n", BOARD_SMMU_UFS_SID);
    }
}

int board_smmu_probe(void)
{
    if (board_smmu.probed) {
        return board_smmu.probe_status;
    }
    if (board_smmu_map() != 0) {
        board_smmu.probe_status = -1;
        return -1;
    }
    board_smmu.scr0 = board_smmu_read32(REG_SCR0);
    board_smmu.id0 = board_smmu_read32(REG_ID0);
    board_smmu.id1 = board_smmu_read32(REG_ID1);
    board_smmu.id2 = board_smmu_read32(REG_ID2);
    board_smmu.sacr = board_smmu_read32(REG_SACR);
    board_smmu.sfsr = board_smmu_read32(REG_SFSR);
    board_smmu_scan();
    board_smmu.probed = 1;
    board_smmu.probe_status = 0;
    return 0;
}

static int board_smmu_pool_physical(void)
{
    uint64_t first = ml_vtophys((vm_offset_t)board_smmu_tables);

    if ((first & (BOARD_SMMU_PAGE_BYTES - 1)) != 0) {
        return -1;
    }
    for (unsigned page = 1; page < BOARD_SMMU_POOL_PAGES; ++page) {
        uint64_t expected = first + page * BOARD_SMMU_PAGE_BYTES;
        uint64_t actual = ml_vtophys((vm_offset_t)board_smmu_tables + page * BOARD_SMMU_PAGE_BYTES);
        if (actual != expected) {
            return -1;
        }
    }
    board_smmu.pool_physical = first;
    return 0;
}

int board_smmu_build_tables(uint64_t iova, uint64_t physical, uint64_t bytes)
{
    ArmSmmu4KRange range = {0};
    ArmSmmu4KConfig *config = &board_smmu_config;

    if (board_smmu_pool_physical() != 0) {
        board_smmu.build_status = -1;
        return -1;
    }
    *config = (ArmSmmu4KConfig){0};
    config->input_bits = 32;
    config->output_bits = 40;
    config->table_physical = board_smmu.pool_physical;
    config->leaf_budget = BOARD_SMMU_POOL_PAGES * 512u;

    range.iova = iova;
    range.physical = physical;
    range.bytes = bytes;
    range.permissions = ArmSmmu4KRead | ArmSmmu4KWrite;
    range.writeback = 1;
    range.shareability = 3;

    board_smmu.last = ArmSmmu4KBuild(config, &range, 1, board_smmu_tables,
                                     BOARD_SMMU_POOL_PAGES, &board_smmu_result);
    board_smmu.build_status = (int)board_smmu.last;
    if (board_smmu.last != ArmSmmu4KSuccess) {
        return -1;
    }
    FlushPoC_Dcache();
    __sync_synchronize();
    board_smmu.built = 1;
    return 0;
}

/* Destructive: program the discovered UFS bank and attach its slot.
 * Needs smmu-attach=1, a clean global fault state and the lease/handler work
 * described in docs/SMMU-BRINGUP.md. */
int board_smmu_attach_ufs(void)
{
    ArmSmmu4KContextConfig context = {0};
    ArmSmmu4KProgramResult program = {0};
    ArmSmmu4KStreamState state = {0};
    ArmSmmu4KStreamResult stream_result = {0};
    ArmSmmu4KStream stream = {0};

    if (board_smmu.attached) {
        return board_smmu.attach_status;
    }
    if (!board_smmu_gate("smmu-attach")) {
        board_smmu.attach_status = 1;
        return board_smmu.attach_status;
    }
    if (board_smmu_probe() != 0 || !board_smmu.built) {
        board_smmu.attach_status = -1;
        return -1;
    }
    if (!board_smmu.ufs_found) {
        printf("SMMU: refusing to attach, sid %03x is not routed\n", BOARD_SMMU_UFS_SID);
        board_smmu.attach_status = -2;
        return board_smmu.attach_status;
    }

    context.lower = &board_smmu_config;
    context.lower_tables = &board_smmu_result;
    context.id0 = board_smmu.id0;
    context.id2 = board_smmu.id2;
    context.asid = 1;
    context.coherent_walk = 1;
    context.stall_on_fault = 0;
    context.continue_after_fault = 0;

    stream.slot = board_smmu.ufs_slot;
    stream.sid = BOARD_SMMU_UFS_SID;

    board_smmu.last = ArmSmmu4KProgramDisabledContext(&board_smmu_io, board_smmu.ufs_bank,
                                                     &context, BOARD_SMMU_POLL_BUDGET, &program);
    if (board_smmu.last != ArmSmmu4KSuccess) {
        printf("SMMU: program bank %u failed %d\n", board_smmu.ufs_bank, (int)board_smmu.last);
        board_smmu.attach_status = -3;
        return board_smmu.attach_status;
    }
    board_smmu.last = ArmSmmu4KAttachStreams(&board_smmu_io, &state, &stream, 1,
                                            board_smmu.ufs_bank, &context, &stream_result);
    printf("SMMU: attach sid %03x slot %u bank %u -> %d\n",
           BOARD_SMMU_UFS_SID, board_smmu.ufs_slot, board_smmu.ufs_bank, (int)board_smmu.last);
    board_smmu.attach_status = (int)board_smmu.last;
    board_smmu.attached = board_smmu.last == ArmSmmu4KSuccess;
    return board_smmu.attach_status;
}

static int board_smmu_gate(const char *argument)
{
    char flag[16];

    return PE_parse_boot_argn(argument, flag, sizeof(flag)) && flag[0] == '1';
}

int board_smmu_self_check(void)
{
    if (!board_smmu.built &&
        board_smmu_build_tables(UINT64_C(0x100000), UINT64_C(0x90000000),
                                (uint64_t)BOARD_SMMU_PAGE_BYTES) != 0) {
        return -1;
    }
    if (!board_smmu_gate("smmu-probe")) {
        return 0;
    }
    return board_smmu_probe();
}

/* Hook NativeUfs.o calls while it brings the controller up. */
void boot_progress_poll(void)
{
    static int reported;

    if (reported) {
        return;
    }
    reported = 1;
    (void)board_smmu_self_check();
    (void)board_smmu_attach_ufs();
}
