// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Board binding for the ArmSmmu4K kit.
//
// The kit builds ARM LPAE stage-1 tables with 4 KiB leaves; it performs no
// MMIO, allocation or TLB work, so the caller owns the table pool and the
// physical addresses. The storage path owns UFS stream 0x60 and needs an
// IOVA window for its DMA buffers: it calls board_smmu_build_tables() with
// the reserved pool and the window, then programs the stream context with
// ArmSmmu4KProgram* once the SMMU registers are mapped.
//
// NativeUfs.o calls boot_progress_poll() while it brings the controller up,
// so the tables are built on the first report and the result stays readable
// through board_smmu_last_result().
#include <stdint.h>

#include "ArmSmmu4K.h"

#define BOARD_SMMU_POOL_PAGES 8u
#define BOARD_SMMU_PAGE_BYTES 4096u

static uint64_t board_smmu_tables[BOARD_SMMU_POOL_PAGES * 512];
static ArmSmmu4KResult board_smmu_result;
static int board_smmu_built;

/* Build the stage-1 tables for one DMA window.
 * pool_physical: reserved, 4 KiB-aligned memory backing board_smmu_tables.
 * iova/physical/bytes: the window, all 4 KiB aligned. */
int board_smmu_build_tables(uint64_t pool_physical, uint64_t iova, uint64_t physical, uint64_t bytes)
{
    ArmSmmu4KConfig config = {0};
    ArmSmmu4KRange range = {0};

    config.input_bits = 32;
    config.output_bits = 40;
    config.table_physical = pool_physical;
    config.leaf_budget = BOARD_SMMU_POOL_PAGES * 512u;

    range.iova = iova;
    range.physical = physical;
    range.bytes = bytes;
    range.permissions = ArmSmmu4KRead | ArmSmmu4KWrite;
    range.writeback = 1;      /* normal write-back */
    range.shareability = 3;   /* inner shareable */

    board_smmu_built = 1;
    return (int)ArmSmmu4KBuild(&config, &range, 1, board_smmu_tables,
                               BOARD_SMMU_POOL_PAGES, &board_smmu_result);
}

const ArmSmmu4KResult *board_smmu_last_result(void)
{
    return &board_smmu_result;
}

/* Called once by the storage bring-up with placeholder physical addresses, so
 * the table code runs before the DMA pool is reserved. */
int board_smmu_self_check(void)
{
    return board_smmu_build_tables(UINT64_C(0x1000000), UINT64_C(0x100000),
                                   UINT64_C(0x90000000), BOARD_SMMU_PAGE_BYTES);
}

/* The board hook NativeUfs.o calls while it brings the controller up: on the
 * first report, build the SMMU tables once. */
void boot_progress_poll(void)
{
    static int reported;

    if (reported || board_smmu_built) {
        return;
    }
    reported = 1;
    (void)board_smmu_self_check();
}
