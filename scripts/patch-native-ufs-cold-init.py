#!/usr/bin/env python3
"""Insert the board UFS cold-init hook into the ShengUfs driver.

The driver is an upstream component (UniXNU/ShengUfs at a pinned revision), so the
board cannot edit it in place. This step runs from [kernel] archives *before*
component:ShengUfs compiles NativeUfs.cpp and makes exactly two insertions:

  * declare board_ufs_cold_init(), implemented in package/storage-bringup/ufs-cold-init.c
  * call it once from NativeUfs::start before any controller access

The call is a no-op unless native-ufs-cold-init=1, which keeps the firmware
takeover path as the default. Re-running the step is harmless.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/ShengUfs/NativeUfs.cpp'
DECLARATION = 'extern "C" int board_ufs_cold_init (void);'
ANCHOR_DECL = 'void boot_progress_poll (void) __attribute__((weak));'
ANCHOR_CALL = '  native_ufs_probe_requested = probe != 0;'
PROBE_LOG = '  printf ("NX741J UFS: start() continues, probe=%d\\n", probe);'
CALL = '  if (board_ufs_cold_init () == 0) { printf ("NX741J UFS: cold init returned 0\\n"); } else { printf ("NX741J UFS: cold init failed\\n"); }'

if not SOURCE.is_file():
    raise SystemExit(f'patch-native-ufs-cold-init: missing {SOURCE}')

text = SOURCE.read_text()
ADOPT_ANCHORS = [
    ('void NativeUfs::adoptAndRead (unsigned level)\n{',
     'void NativeUfs::adoptAndRead (unsigned level)\n{\n  printf ("NATIVE_UFS adopt: enter level=%u\\n", level);'),
    ('  uint64_t fwList = (uint64_t (read32 (hc, 0x54)) << 32) | read32 (hc, 0x50);',
     '  uint64_t fwList = (uint64_t (read32 (hc, 0x54)) << 32) | read32 (hc, 0x50);\n  printf ("NATIVE_UFS adopt: fwList=%llx\\n", fwList);'),
    ('  uint8_t *virt = reinterpret_cast<uint8_t *> (regionMap->getVirtualAddress ());',
     '  printf ("NATIVE_UFS adopt: list mapped\\n");\n  uint8_t *virt = reinterpret_cast<uint8_t *> (regionMap->getVirtualAddress ());'),
    ('  uint32_t hcsBefore = read32 (hc, 0x30);',
     '  printf ("NATIVE_UFS adopt: cleared, reading state\\n");\n  uint32_t hcsBefore = read32 (hc, 0x30);'),
]
ADOPT_ANCHORS += [
    ('  const uint32_t ucdOffset = 4096, rspOffset = 512, prdtOffset = 1024, dataOffset = 8192;',
     '  /* data inside 8K: the firmware queue block looks like 8 KiB, so +8K is out of it */\n  const uint32_t ucdOffset = 4096, rspOffset = 512, prdtOffset = 1024, dataOffset = 6144;'),

    ('    uint8_t rd[10] = { 0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0 };',
     '    printf ("NATIVE_UFS gpt: reading header bs=%08x\\n", blockSize[0]);\n    uint8_t rd[10] = { 0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0 };'),
    ('    if (h.ocs != 0 || h.scsi != 0) {',
     '    printf ("NATIVE_UFS gpt: header done ocs=%x resp=%x scsi=%x\\n", h.ocs, h.response, h.scsi);\n    if (h.ocs != 0 || h.scsi != 0) {'),
    ('    if (!done) {',
     '    printf ("NATIVE_UFS cmd: done=%d dbr=%08x is=%08x ocs=%02x\\n", done ? 1 : 0, read32 (hc, 0x58), read32 (hc, 0x20), *reinterpret_cast<volatile uint32_t *> (utrd + 8) & 0xffU);\n    if (!done) {'),

    ('  for (unsigned lun = 0; lun < maxLun; lun++) {',
     '  for (unsigned lun = 0; lun < ((level == 9) ? 0u : maxLun); lun++) {   /* level 9 skips the LUN loop, it hangs here */'),
    ('  if (level == 9) {',
     '  if (level == 9 && blockSize[0] != 4096) {\n    unsigned nx_off = 0, nx_hits = 0;\n    for (unsigned nx_i = 0; nx_i + 4 <= 16384; nx_i += 4) {\n      if (virt[nx_i] == 0 && virt[nx_i + 1] == 0 && virt[nx_i + 2] == 0x10 && virt[nx_i + 3] == 0) { nx_hits++; nx_off = nx_i; }\n    }\n    panic ("NATIVE_UFS gpt: blockSize=%08x cap-data at %u hits=%u d0=%08x d1=%08x", blockSize[0], nx_off, nx_hits, *(volatile uint32_t *)(virt + dataOffset), *(volatile uint32_t *)(virt + 0));\n  }\n  if (level == 9) {'),

    ('    *reinterpret_cast<uint32_t *> (utrd + 0) = 0x10000000U | (direction << 25);',
     '    /* interrupt bit (24) as well: UFSHCI only completes descriptors that ask for it */\n    *reinterpret_cast<uint32_t *> (utrd + 0) = 0x10000000U | (1U << 24) | (direction << 25);'),
    ('    *reinterpret_cast<uint32_t *> (utrd + 0) = 0x10000000U;',
     '    *reinterpret_cast<uint32_t *> (utrd + 0) = 0x10000000U | (1U << 24);'),

    ('    uintptr_t smmu = mapPhys (0x15000000ULL, 0xd0000, smmuMap);',
     '    /* smmu small page: GR0 fits one 4 KiB page; a larger mapPhys fails here. */\n    uintptr_t smmu = mapPhys (0x15000000ULL, 0x1000, smmuMap);'),

    ('  OSSharedPtr<IOMemoryMap> smmuMap;',
     '  printf ("NATIVE_UFS adopt: doorbell ok hcs=%08x db=%08x\\n", hcsBefore, doorbellBefore);\n  OSSharedPtr<IOMemoryMap> smmuMap;'),
    ('  if (read32 (hc, 0x60) & 1) panic ("NATIVE_UFS adopt: request list did not stop");',
     '  printf ("NATIVE_UFS adopt: list stop loop done rsr=%08x\\n", read32 (hc, 0x60));\n  if (read32 (hc, 0x60) & 1) panic ("NATIVE_UFS adopt: request list did not stop");'),
    ('  if (!keepQueue) {',
     '  printf ("NATIVE_UFS adopt: smmu map=%p idr2=%08x keep=%u\\n", (void *)smmu, smmuReport[2], keepQueue);\n  if (!keepQueue) {'),
    ('  write32 (hc, 0x20, 0xffffffffU);  // clear stale status',
     '  printf ("NATIVE_UFS adopt: base written\\n");\n  write32 (hc, 0x20, 0xffffffffU);  // clear stale status'),
    ('  if (level == 3) {',
     '  printf ("NATIVE_UFS adopt: list started hcs=%08x rsr=%08x\\n", read32 (hc, 0x30), read32 (hc, 0x60));\n  if (level == 3) {'),
]
ADOPT_ANCHORS += [
    ('    if (smmu) { smmuReport[2] = read32 (smmu, 0x24); }',
     '    if (smmu) {\n      smmuReport[2] = read32 (smmu, 0x24);\n      write32 (smmu, 0x800, 0x80000060u);\n      write32 (smmu, 0xc00, 1u);\n      __asm__ volatile ("dsb sy" ::: "memory");\n      printf ("NATIVE_UFS adopt: smr=%08x s2cr=%08x\\n", read32 (smmu, 0x800), read32 (smmu, 0xc00));\n    }'),
]
SPIN = '{ volatile unsigned nx_spin = 0; for (nx_spin = 0; nx_spin < 4000; nx_spin++) { __asm__ volatile ("" ::: "memory"); } }'
SPIN_LONG = '{ volatile unsigned nx_spin = 0; for (nx_spin = 0; nx_spin < 400000; nx_spin++) { __asm__ volatile ("" ::: "memory"); } }'
REPL = [
    ('static void cacheClean (const void *start, size_t length)',
     'static void __attribute__((unused)) cacheClean (const void *start, size_t length)'),
    ('static void cacheCleanInvalidate (const void *start, size_t length)',
     'static void __attribute__((unused)) cacheCleanInvalidate (const void *start, size_t length)'),
    ('cacheClean (virt, 16384);',
     'flush_dcache64 ((addr64_t)phys, 16384, TRUE);'),
    ('cacheCleanInvalidate (virt, 16384);',
     'flush_dcache64 ((addr64_t)phys, 16384, TRUE);'),
    ('IODelay (1);', SPIN),
    ('IODelay (10);', SPIN),
    ('IODelay (1000);', SPIN_LONG),
    ('IODelay (500);', SPIN_LONG),
]
for old, new in REPL:
    text = text.replace(old, new)

for old, new in ADOPT_ANCHORS:
    if old in text and new not in text:
        text = text.replace(old, new, 1)


if ANCHOR_DECL not in text or ANCHOR_CALL not in text:
    raise SystemExit('patch-native-ufs-cold-init: anchors not found; driver changed')

text = text.replace(ANCHOR_DECL, ANCHOR_DECL + '\n' + DECLARATION, 1)
text = text.replace(ANCHOR_CALL, CALL + '\n' + PROBE_LOG + '\n' + ANCHOR_CALL, 1)
SOURCE.write_text(text)
print('== patch-native-ufs-cold-init: applied')
