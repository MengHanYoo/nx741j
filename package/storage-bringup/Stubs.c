// SPDX-License-Identifier: BSD-2-Clause-Patent
// Bring-up anchors for the source-built UFS adapter.
//
// The real verification code comes from kits/VerifiedRoot (verified_root.c +
// adapters/xnu_crypto.c). These definitions only anchor the owner key and the policy
// table: no policy bundle is compiled in yet, verified_root_policy_count stays 0 and
// verification fails closed. The optional board diagnostic is a no-op until its
// component is built in.
#include <stdint.h>

#define VR_KEY_BYTES 395u

void boot_progress_poll(void) {}

uint8_t verified_root_key[VR_KEY_BYTES];
uint64_t verified_root_policy_count;
void *verified_root_policies[16];
