// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Board binding for the ArmSmmu4K kit (qsmmu-v500 at 0x15000000).
//
// Three stages, each one only runs when the previous one succeeded:
//
//   map      ml_io_map() the SMMU registers and hand the live mapping to
//            ArmSmmu4KBindMmio(). Read-only.
//   probe    read ID0/ID1/ID2 through that mapping. Read-only, and the only
//            step that runs by default.
//   attach   build the stage-1 tables into a page-aligned pool, publish them,
//            program a disabled context bank and attach UFS stream 0x60.
//            Destructive: it needs the bank/slot lease, a fault handler and
//            the vendor stream map, so it is gated behind the "smmu-attach"
//            boot argument and stays off unless someone asks for it.
//
// NativeUfs.o calls boot_progress_poll() while it brings the controller up,
// which is where the probe (and, with the boot argument, the attach) runs.
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
#define BOARD_SMMU_UFS_BANK      0u
#define BOARD_SMMU_UFS_ASID      1u
#define BOARD_SMMU_POLL_BUDGET   1000u

typedef struct {
    int mapped, bound, probed, built, attached;
    int map_status, bind_status, probe_status, build_status, program_status, attach_status;
    uint32_t id0, id1, id2, scr0;
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

const board_smmu_state_t *board_smmu_status(void)
{
    return &board_smmu;
}

const ArmSmmu4KResult *board_smmu_last_result(void)
{
    return &board_smmu_result;
}

static int board_smmu_map(void)
{
    vm_offset_t mapping;

    if (board_smmu.mapped) {
        return board_smmu.map_status;
    }
    mapping = ml_io_map((vm_offset_t)BOARD_SMMU_BASE, (vm_size_t)BOARD_SMMU_BYTES);
    if (!mapping) {
        board_smmu.map_status = -1;
        return -1;
    }
    board_smmu_mmio.mapping = (volatile unsigned char *)mapping;
    board_smmu_mmio.bytes = (size_t)BOARD_SMMU_BYTES;
    board_smmu.last = ArmSmmu4KBindMmio(&board_smmu_mmio, &board_smmu_io);
    board_smmu.bind_status = (int)board_smmu.last;
    board_smmu.mapped = 1;
    board_smmu.bound = board_smmu.last == ArmSmmu4KSuccess;
    board_smmu.map_status = board_smmu.bound ? 0 : -1;
    return board_smmu.map_status;
}

/* Read-only: the identification registers prove the block answers MMIO. */
static int board_smmu_probe(void)
{
    if (board_smmu.probed) {
        return board_smmu.probe_status;
    }
    if (board_smmu_map() != 0) {
        board_smmu.probe_status = -1;
        return -1;
    }
    if (board_smmu_io.read32(board_smmu_io.cookie, 0x0, &board_smmu.id0) ||
        board_smmu_io.read32(board_smmu_io.cookie, 0x4, &board_smmu.id1) ||
        board_smmu_io.read32(board_smmu_io.cookie, 0x8, &board_smmu.id2)) {
        board_smmu.probe_status = -1;
        return -1;
    }
    (void)board_smmu_io.read32(board_smmu_io.cookie, 0xc, &board_smmu.scr0);
    board_smmu.probed = 1;
    board_smmu.probe_status = 0;
    return 0;
}

/* The pool has to be physically contiguous: check every page with the same
 * translation the hardware will use. */
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

/* Build and publish the stage-1 tables for one DMA window. */
int board_smmu_build_tables(uint64_t iova, uint64_t physical, uint64_t bytes)
{
    ArmSmmu4KConfig *config = &board_smmu_config;
    ArmSmmu4KRange range = {0};

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
    /* Publish the tables the device will walk. */
    FlushPoC_Dcache();
    __sync_synchronize();
    board_smmu.built = 1;
    return 0;
}

/* Destructive: program the UFS context bank and attach SID 0x60.
 * Requires the bank lease, a quiesced client and the vendor stream map, so it
 * only runs when "smmu-attach=1" is on the boot command line. */
int board_smmu_attach_ufs(void)
{
    ArmSmmu4KContextConfig context = {0};
    ArmSmmu4KProgramResult program = {0};
    ArmSmmu4KStreamState state = {0};
    ArmSmmu4KStreamResult stream_result = {0};
    ArmSmmu4KStream stream = {.slot = BOARD_SMMU_UFS_SID, .sid = BOARD_SMMU_UFS_SID};
    if (board_smmu.attached) {
        return board_smmu.attach_status;
    }
    if (!board_smmu_gate("smmu-attach")) {
        board_smmu.attach_status = 1;   /* gated off */
        return board_smmu.attach_status;
    }
    if (board_smmu_probe() != 0 || !board_smmu.built) {
        board_smmu.attach_status = -1;
        return -1;
    }

    context.lower = &board_smmu_config;
    context.lower_tables = &board_smmu_result;
    context.id0 = board_smmu.id0;
    context.id2 = board_smmu.id2;
    context.asid = BOARD_SMMU_UFS_ASID;
    context.coherent_walk = 1;
    context.stall_on_fault = 0;
    context.continue_after_fault = 0;

    board_smmu.last = ArmSmmu4KProgramDisabledContext(&board_smmu_io, BOARD_SMMU_UFS_BANK,
                                                     &context, BOARD_SMMU_POLL_BUDGET, &program);
    board_smmu.program_status = (int)board_smmu.last;
    if (board_smmu.last != ArmSmmu4KSuccess) {
        board_smmu.attach_status = -2;
        return board_smmu.attach_status;
    }
    board_smmu.last = ArmSmmu4KAttachStreams(&board_smmu_io, &state, &stream, 1,
                                            BOARD_SMMU_UFS_BANK, &context, &stream_result);
    board_smmu.attach_status = (int)board_smmu.last;
    board_smmu.attached = board_smmu.last == ArmSmmu4KSuccess;
    return board_smmu.attach_status;
}

/* MMIO stays untouched unless the boot command line asks for it: an unclocked
 * block would make the first register read hang the machine. */
static int board_smmu_gate(const char *argument)
{
    char flag[16];

    return PE_parse_boot_argn(argument, flag, sizeof(flag)) && flag[0] == '1';
}

/* Bootstrap: build the tables once; touch MMIO only under "smmu-probe=1". */
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
