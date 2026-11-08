// SPDX-License-Identifier: BSD-2-Clause-Patent
// Link-time bring-up stubs for the source-built UFS adapter.
//
// The ABI reference object (IOStorageFamily / IOBlockStorageDevice) is built by the
// official audit step. These stubs satisfy only the remaining symbols of the sheng
// storage stack (verified-root policy handoff and the optional board diagnostic) so a
// bring-up kernel can link before the private storage-policy kit is available.
// Runtime is gated: SourceStorage only starts with native-source-storage=1.
#include <stdint.h>

void boot_progress_poll(void) {}

void vr_open(void) {}
void vr_verify_block(void) {}

uint8_t vr_xnu_crypto[256];
uint8_t verified_root_key[32];
uint64_t verified_root_policy_count;
void *verified_root_policies[16];
