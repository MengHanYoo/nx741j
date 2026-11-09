// SPDX-License-Identifier: BSD-2-Clause-Patent
// Bring-up anchors for the source-built UFS adapter.
//
// The real verification code comes from kits/VerifiedRoot (verified_root.c +
// adapters/xnu_crypto.c). These definitions only anchor the owner key and the policy
// table. With --policy they are replaced by Policy.o, which carries a real owner key
// and a signed envelope; without one they stay zero and verification fails closed,
// because verified_root_policy_count is 0.
#include <stdint.h>

#define VR_KEY_BYTES 395u

void boot_progress_poll(void) {}

#ifndef VR_POLICY_BUILT_IN
uint8_t verified_root_key[VR_KEY_BYTES];
uint64_t verified_root_policy_count;
void *verified_root_policies[16];
#endif
