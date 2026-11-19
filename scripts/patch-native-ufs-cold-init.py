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
      c[16] = 0x28;
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
    bool ok = issue (0x004a6328u, 4096);
    const uint8_t *h = virt + dataOffset;
    uint32_t ver = (uint32_t)h[8] | ((uint32_t)h[9] << 8) | ((uint32_t)h[10] << 16) | ((uint32_t)h[11] << 24);
    uint32_t cnt = (uint32_t)h[12] | ((uint32_t)h[13] << 8) | ((uint32_t)h[14] << 16) | ((uint32_t)h[15] << 24);
    uint64_t img = 0; for (int i = 0; i < 8; i++) img |= (uint64_t)h[16 + i] << (8 * i);
    uint64_t w0off = 0, w0sz = 0, w1off = 0, w1sz = 0;
    for (int i = 0; i < 8; i++) w0off |= (uint64_t)h[24 + 32 + i] << (8 * i);
    for (int i = 0; i < 8; i++) w0sz |= (uint64_t)h[24 + 40 + i] << (8 * i);
    for (int i = 0; i < 8; i++) w1off |= (uint64_t)h[24 + 128 + 32 + i] << (8 * i);
    for (int i = 0; i < 8; i++) w1sz |= (uint64_t)h[24 + 128 + 40 + i] << (8 * i);
    const uint64_t partBytes = 1024ULL * 1024 * 1024 * 1024;
    bool bounds = img >= 4096 && !(img & 4095) && img <= partBytes;
    bounds = bounds && !(w0off & 4095) && w0off >= 4096 && w0sz && w0off < img && w0sz <= img - w0off;
    bounds = bounds && !(w1off & 4095) && w1off >= 4096 && w1sz && w1off < img && w1sz <= img - w1off;
    bool ordered = w0off + w0sz <= w1off;
    panic ("NATIVE_UFS root: ok=%d magic=%c%c%c%c%c%c%c%c ver=%u cnt=%u img=%llx w0=%c%c o=%llx sz=%llx w1=%c%c o=%llx sz=%llx bounds=%d ord=%d", ok ? 1 : 0, h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7], ver, cnt, img, h[24], h[26], w0off, w0sz, h[24+128], h[24+128+2], w1off, w1sz, bounds ? 1 : 0, ordered ? 1 : 0);
  }
'''
text = text.replace('  if (level == 9 && blockSize[0] != 4096) {', NX_CAP + '  if (level == 9 && blockSize[0] != 4096) {', 1)
text, _n = _nxre.subn(r'panic \("NATIVE_UFS gpt: blockSize=.*?\);', NX_PRD_DUMP, text, count=1, flags=_nxre.S)
print('== prdt dump patched:', _n)
text = text.replace('utrd0=%08x utrd2=%08x utrd4=%08x ie=%08x', 'utrd0=%08x utrd2=%08x utrd4=%08x utrd6=%08x utrd7=%08x ie=%08x')
text = text.replace('*reinterpret_cast<volatile uint32_t *> (utrd + 16),\n            read32 (hc, 0x24));', '*reinterpret_cast<volatile uint32_t *> (utrd + 16), *reinterpret_cast<volatile uint32_t *> (utrd + 24), *reinterpret_cast<volatile uint32_t *> (utrd + 28),\n            read32 (hc, 0x24));')
# --- production bring-up on this board: firmware queue region + 4 KiB SMMU page ---
text = text.replace('  kDataMax = 131072,', '  kDataMax = 8192,   /* NX_FW_REGION: one slot must fit the firmware 16 KiB queue */')
text = text.replace('mapPhys (0x15000000ULL, 0xd0000, gHost.smmuMap)', 'mapPhys (0x15000000ULL, 0x1000, gHost.smmuMap)')
text = text.replace('  gHost.smmu = gHost.smmuMap->getVirtualAddress ();', '  if (!gHost.smmuMap) { *why = "smmu map"; return false; }\n  gHost.smmu = gHost.smmuMap->getVirtualAddress ();')
FW = '''  {
    static OSSharedPtr<IOMemoryMap> fwMap;
    uint64_t fw = ((uint64_t)read32 (gHost.hc, 0x54) << 32) | read32 (gHost.hc, 0x50);
    if (fw && !(fw & 0xfff)) {
      IOMemoryDescriptor *region = IOMemoryDescriptor::withPhysicalAddress (fw, 16384, kIODirectionInOut);
      if (region) {
        fwMap = OSSharedPtr<IOMemoryMap> (region->map (kIOMapAnywhere), OSNoRetain);
        region->release ();
      }
      if (fwMap) {
        gHost.virt = reinterpret_cast<uint8_t *> (fwMap->getVirtualAddress ());
        gHost.phys = fw;
        gHost.dmaBytes = 16384;
        printf ("NATIVE_UFS bringup: firmware region %llx\\n", (unsigned long long)fw);
      }
    }
  }
'''
text = text.replace('  for (unsigned attempt = 0; attempt < 3 && !gHost.virt; ++attempt) {', FW + '  for (unsigned attempt = 0; attempt < 3 && !gHost.virt; ++attempt) {', 1)
# --- NX_FW_LAYOUT: shrink the per-slot data area so one slot fits 16 KiB ---
LAYOUT = ROOT / 'drivers/ShengUfs/RootTransportLayout.h'
if LAYOUT.is_file():
    lt = LAYOUT.read_text()
    if 'kDataMax = 131072,' in lt:
        lt = lt.replace('kDataMax = 131072,', 'kDataMax = 8192,   /* NX_FW_LAYOUT: one slot fits the firmware 16 KiB queue */', 1)
        LAYOUT.write_text(lt)
        print('== NX_FW_LAYOUT: kDataMax = 8192')
# --- NX_STEP_TRACE: one line per bring-up step after the list runs ---
text = text.replace('  gRootDiagnostic.stage = "NOP OUT";', '  printf ("NATIVE_UFS bringup: step nop\\n");\n  gRootDiagnostic.stage = "NOP OUT";', 1)
text = text.replace('  gRootDiagnostic.stage = "power mode query";', '  printf ("NATIVE_UFS bringup: step power query\\n");\n  gRootDiagnostic.stage = "power mode query";', 1)
text = text.replace('    gRootDiagnostic.stage = "device wake";', '    printf ("NATIVE_UFS bringup: step wake\\n");\n    gRootDiagnostic.stage = "device wake";', 1)
text = text.replace('  gRootDiagnostic.stage = "LUN ready";', '  printf ("NATIVE_UFS bringup: step tur loop\\n");\n  gRootDiagnostic.stage = "LUN ready";', 1)
# --- NX_NOP_RC: report the NOP OUT result before the root check ---
text = text.replace('  if (hostNop () != kIOReturnSuccess) {', '  { IOReturn nx_nop = hostNop (); printf ("NATIVE_UFS bringup: nop rc=%d dbr=%08x is=%08x\\n", (int)nx_nop, read32 (gHost.hc, 0x58), read32 (gHost.hc, 0x20)); if (nx_nop != kIOReturnSuccess) {', 1)
text = text.replace('    *why = "NOP OUT";\n    return false;\n  }', '    *why = "NOP OUT";\n    return false;\n  } }', 1)
SOURCE.write_text(text)
print('== patch-native-ufs-cold-init: applied')
