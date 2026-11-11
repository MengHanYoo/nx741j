// SPDX-License-Identifier: BSD-2-Clause-Patent
// Bring-up anchors for the source-built UFS adapter.
//
// The real verification code comes from kits/VerifiedRoot (verified_root.c +
// adapters/xnu_crypto.c). These definitions only anchor the owner key and the policy
// table. With --policy they are replaced by Policy.o, which carries a real owner key
// and a signed envelope; without one they stay zero and verification fails closed,
// because verified_root_policy_count is 0.
//
// The board diagnostic hook the storage driver calls (boot_progress_poll) lives in
// SmmuBind.c when the ArmSmmu4K kit is bound, so the SMMU tables are built on the
// first progress report. VR_SMMU_BOUND keeps this file from defining it twice.
#include <stdint.h>

#define VR_KEY_BYTES 395u

#ifndef VR_SMMU_BOUND
void boot_progress_poll(void) {}
#endif

#ifndef VR_POLICY_BUILT_IN
uint8_t verified_root_key[VR_KEY_BYTES];
uint64_t verified_root_policy_count;
void *verified_root_policies[16];
#endif

// Board cold-start path (native-ufs-cold-init=1). NativeUfs::start calls
// board_ufs_cold_init() through the patch-native-ufs-cold-init step; the definition
// rides in this member so the archive build needs no extra object.
#include "ufs-cold-init.c"
