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
import re as _nxre
NX_PRD_DUMP = ('panic ("NATIVE_UFS gpt: bs=%08x off=%u hits=%u d0=%08x utrd7=%08x prd0=%08x prd3=%08x prdA=%08x rsp0=%08x resid=%08x rsp16=%08x", blockSize[0], nx_off, nx_hits, *(volatile uint32_t *)(virt + dataOffset), *(volatile uint32_t *)(virt + 28), *(volatile uint32_t *)(virt + ucdOffset + prdtOffset + 0), *(volatile uint32_t *)(virt + ucdOffset + prdtOffset + 12), (uint32_t)(phys + dataOffset), *(volatile uint32_t *)(virt + ucdOffset + rspOffset + 0), *(volatile uint32_t *)(virt + ucdOffset + rspOffset + 12), *(volatile uint32_t *)(virt + ucdOffset + rspOffset + 16));')
NX_CAP = r'''  if (level == 9) {
    uint8_t *u = virt; uint8_t *c = virt + ucdOffset;
    auto issue = [&] (uint32_t lba, uint32_t bytes) -> bool {
      zero32 (u, 32); zero32 (c, 1024 + 16); zero32 (virt + dataOffset, bytes + 64);
      c[0] = 0x01; c[1] = 0x40; c[2] = 0; c[3] = 1; be32 (c + 12, bytes);
      c[16] = 0x28; c[21] = (uint8_t)(lba & 0xff); c[22] = 0; c[23] = (uint8_t)((bytes / 4096) & 0xff);
      /* READ(10): LBA at bytes 2..5 (big endian), transfer length at 7..8 */
      c[16 + 2] = (uint8_t)(lba >> 24); c[16 + 3] = (uint8_t)(lba >> 16); c[16 + 4] = (uint8_t)(lba >> 8); c[16 + 5] = (uint8_t)lba;
      c[16 + 7] = (uint8_t)((bytes / 4096) >> 8); c[16 + 8] = (uint8_t)(bytes / 4096);
      *(uint32_t *)(u + 0) = 0x11000000U; *(uint32_t *)(u + 8) = 0x0fU;
      *(uint32_t *)(u + 16) = (uint32_t)(phys + ucdOffset); *(uint32_t *)(u + 20) = (uint32_t)((phys + ucdOffset) >> 32);
      *(uint32_t *)(u + 24) = (512U >> 2) | ((rspOffset >> 2) << 16);
      *(uint32_t *)(u + 28) = 1U | ((prdtOffset >> 2) << 16);
      *(uint32_t *)(c + prdtOffset + 0) = (uint32_t)(phys + dataOffset);
      *(uint32_t *)(c + prdtOffset + 4) = (uint32_t)((phys + dataOffset) >> 32);
      *(uint32_t *)(c + prdtOffset + 8) = 0; *(uint32_t *)(c + prdtOffset + 12) = bytes - 1;
      flush_dcache64 ((addr64_t)phys, 16384, TRUE);
      write32 (hc, 0x20, 0xffffffffU); write32 (hc, 0x58, 1);
      bool d = false;
      for (unsigned cl = 0; cl < 400000; cl++) { if (!(read32 (hc, 0x58) & 1)) { d = true; break; } { volatile unsigned s = 0; for (s = 0; s < 4000; s++) { __asm__ volatile ("" ::: "memory"); } } }
      flush_dcache64 ((addr64_t)phys, 16384, TRUE);
      return d;
    };
    bool hOk = issue (1, 4096);
    const uint8_t *h = virt + dataOffset;
    uint64_t entLba = 0; for (int i = 0; i < 8; i++) entLba |= (uint64_t)h[72 + i] << (8 * i);
    uint32_t entCnt = (uint32_t)h[80] | ((uint32_t)h[81] << 8) | ((uint32_t)h[82] << 16) | ((uint32_t)h[83] << 24);
    bool eOk = issue ((uint32_t)entLba, 4096);
    const uint8_t *e = virt + dataOffset;
    int found = -1; uint64_t first = 0, last = 0; unsigned shown = 0; char nm[9];
    for (uint32_t i = 0; i < entCnt && i < 32; i++) {
      const uint8_t *p0 = e + i * 128;
      if (p0[0] == 0 && p0[1] == 0 && p0[2] == 0 && p0[3] == 0) continue;
      const uint8_t *n = p0 + 56;
      if (n[0] == 'u' && n[2] == 's' && n[4] == 'e' && n[6] == 'r' && n[8] == 'd' && n[10] == 'a' && n[12] == 't' && n[14] == 'a') {
        found = (int)i;
        for (int k = 0; k < 8; k++) first |= (uint64_t)p0[32 + k] << (8 * k);
        for (int k = 0; k < 8; k++) last |= (uint64_t)p0[40 + k] << (8 * k);
      }
      if (i < 3) { for (int k = 0; k < 8; k++) nm[k] = (char)n[k * 2]; nm[8] = 0; if (i == 0) { for (int k = 0; k < 8; k++) nm[k] = (char)(e[56 + k * 2]); } }
      shown++;
    }
    panic ("NATIVE_UFS gpt2: hdr=%d ents=%d entlba=%llx cnt=%u used=%u userdata=%d first=%llx last=%llx n0=%02x%02x%02x%02x", hOk ? 1 : 0, eOk ? 1 : 0, entLba, entCnt, shown, found, first, last, e[56], e[58], e[60], e[62]);
  }
'''
text = text.replace('  if (level == 9 && blockSize[0] != 4096) {', NX_CAP + '  if (level == 9 && blockSize[0] != 4096) {', 1)
text, _n = _nxre.subn(r'panic \("NATIVE_UFS gpt: blockSize=.*?\);', NX_PRD_DUMP, text, count=1, flags=_nxre.S)
print('== prdt dump patched:', _n)
text = text.replace('utrd0=%08x utrd2=%08x utrd4=%08x ie=%08x', 'utrd0=%08x utrd2=%08x utrd4=%08x utrd6=%08x utrd7=%08x ie=%08x')
text = text.replace('*reinterpret_cast<volatile uint32_t *> (utrd + 16),\n            read32 (hc, 0x24));', '*reinterpret_cast<volatile uint32_t *> (utrd + 16), *reinterpret_cast<volatile uint32_t *> (utrd + 24), *reinterpret_cast<volatile uint32_t *> (utrd + 28),\n            read32 (hc, 0x24));')
SOURCE.write_text(text)
print('== patch-native-ufs-cold-init: applied')
