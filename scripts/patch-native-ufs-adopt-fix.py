#!/usr/bin/env python3
"""Board patches for ShengUfs at the revision the UFS porting guide pins.

Applied to the pristine fd420df0 checkout before component:ShengUfs compiles:

1) SMMU: (pages+banks)*4096 needs 0xD1000 while the mapping is 0xD0000 on this
   unit (IDR1=0x60000051).  Stream 0x60 has no match entry and SCR0 bit0=0, so
   both the adopt path and the production bring-up write S2CR[0x60]=1 instead of
   planning a context bank.
2) The adopt path cleaned 32 KiB out of a 16 KiB mapping; narrow those cleans.
3) printf progress markers in hostBringUp and around the doorbell: printf reaches
   the loader log, panic() does not on this kernel.
4) Interrupt mode stays off: the ArmCpu SPI helpers this revision expects are not
   exported by the kernel we build, and polling is the documented fallback.

Re-running is harmless.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/ShengUfs/NativeUfs.cpp'
HEAD = 'void NativeUfs::adoptAndRead (unsigned level)'
WIDE = 'cacheClean (virt, 32768)'
WIDE_INV = 'cacheCleanInvalidate (virt, 32768)'
NARROW = 'cacheClean (virt, 16384)'
NARROW_INV = 'cacheCleanInvalidate (virt, 16384)'
NL = chr(92) + 'n'
TAB = chr(10)

def q(s):
    return s

SMMU_CALL = '  smmuBypassStream (0x60, smmuMap, smmuReport, phys, 16384);'
SMMU_PLAIN = (
    "  /*" + TAB +
    "   * IDR1=0x60000051: pages=128, banks=81 -> the context-bank window would need" + TAB +
    "   * 0xD1000 while the mapping is 0xD0000.  Stream 0x60 has no match entry" + TAB +
    "   * (SMR=0, S2CR=0), so bypass it directly." + TAB +
    "   */" + TAB +
    "  {" + TAB +
    "    /*" + TAB +
    "     * The bootloader already points stream 0x60 at CB0 with SCTLR.M=0, so the" + TAB +
    "     * host's DMA bypasses translation (verified on the pandora SM8850 port:" + TAB +
    "     * SMR0=stream 0x60, S2CR=0, CB0 SCTLR=0x0a9f00e0).  Writing S2CR here broke" + TAB +
    "     * that mapping and made the controller's descriptor fetch hang the bus." + TAB +
    "     */" + TAB +
    "    uintptr_t smmu = mapPhys (0x15000000ULL, 0xd0000, smmuMap);" + TAB +
    "    smmuReport[0] = 0; smmuReport[1] = 0; smmuReport[2] = 0;" + TAB +
    "    if (smmu) { smmuReport[2] = read32 (smmu, 0x24); }" + TAB +
    "  }"
)

PROD_CALL = '  smmuBypassStream (0x60, gHost.smmuMap, report, gHost.phys, gHost.dmaBytes);'
PROD_PLAIN = (
    "  {" + TAB +
    "    /* Stream 0x60 already bypasses (CB0, SCTLR.M=0); do not touch SMMU." + TAB +
    "     * Writing S2CR here is what broke the descriptor fetch." + TAB +
    "     */" + TAB +
    "    uintptr_t smmu = mapPhys (0x15000000ULL, 0xd0000, gHost.smmuMap);" + TAB +
    "    report[0] = 0; report[1] = 0; report[2] = 0;" + TAB +
    "    if (smmu) { report[2] = read32 (smmu, 0x24); }" + TAB +
    "  }"
)

RING = '    write32 (hc, 0x58, 1);          // ring slot 0'
RING_PROBE = (
    '    printf ("NATIVE_UFS ring-before hcs=%08x rsr=%08x dbr=%08x is=%08x utrlba=%08x utrd0=%08x utrd2=%08x utrd4=%08x ie=%08x' + NL + '",' + TAB +
    "            read32 (hc, 0x30), read32 (hc, 0x60), read32 (hc, 0x58), read32 (hc, 0x20)," + TAB +
    "            read32 (hc, 0x50)," + TAB +
    "            *reinterpret_cast<volatile uint32_t *> (utrd + 0)," + TAB +
    "            *reinterpret_cast<volatile uint32_t *> (utrd + 8)," + TAB +
    "            *reinterpret_cast<volatile uint32_t *> (utrd + 16)," + TAB +
    "            read32 (hc, 0x24));" + TAB +
    "    write32 (hc, 0x58, 1);          // ring slot 0" + TAB +
    '    printf ("NATIVE_UFS ring-after  hcs=%08x rsr=%08x dbr=%08x is=%08x ocs=%02x' + NL + '",' + TAB +
    "            read32 (hc, 0x30), read32 (hc, 0x60), read32 (hc, 0x58), read32 (hc, 0x20)," + TAB +
    "            *reinterpret_cast<volatile uint32_t *> (utrd + 8) & 0xffU);"
)

# "Ring only" variant: native-ufs-keepqueue=1 leaves the firmware's request list
# running and untouched.  The pandora SM8850 port never stops or restarts the
# list, so this isolates whether our stop/restart makes the doorbell write hang.
KEEPQ_DECL = '  uint32_t smmuReport[3] = { 0, 0, 0 };'
KEEPQ_DECL_FIX = ('  uint32_t smmuReport[3] = { 0, 0, 0 };' + TAB +
                  '  unsigned keepQueue = 0;' + TAB +
                  '  PE_parse_boot_argn ("native-ufs-keepqueue", &keepQueue, sizeof (keepQueue));')
KEEPQ_STOP = ('  write32 (hc, 0x60, 0);' + TAB +
              '  __asm__ volatile ("dsb sy" ::: "memory");' + TAB +
              '  unsigned stopLoops = 10000;' + TAB +
              '  while ((read32 (hc, 0x60) & 1) && stopLoops--) IODelay (10);' + TAB +
              '  if (read32 (hc, 0x60) & 1) panic ("NATIVE_UFS adopt: request list did not stop");')
KEEPQ_STOP_FIX = ('  if (!keepQueue) {' + TAB +
                  '    write32 (hc, 0x60, 0);' + TAB +
                  '    __asm__ volatile ("dsb sy" ::: "memory");' + TAB +
                  '    unsigned stopLoops = 10000;' + TAB +
                  '    while ((read32 (hc, 0x60) & 1) && stopLoops--) IODelay (10);' + TAB +
                  '    if (read32 (hc, 0x60) & 1) panic ("NATIVE_UFS adopt: request list did not stop");' + TAB +
                  '  }')
KEEPQ_BASE = ('  write32 (hc, 0x50, static_cast<uint32_t> (phys));' + TAB +
              '  write32 (hc, 0x54, static_cast<uint32_t> (phys >> 32));')
KEEPQ_BASE_FIX = ('  if (!keepQueue) {' + TAB +
                  '    write32 (hc, 0x50, static_cast<uint32_t> (phys));' + TAB +
                  '    write32 (hc, 0x54, static_cast<uint32_t> (phys >> 32));' + TAB +
                  '  }')
KEEPQ_START = ('  write32 (hc, 0x20, 0xffffffffU);  // clear stale status' + TAB +
               '  write32 (hc, 0x60, 1);' + TAB +
               '  if (!waitSet (hc, 0x30, 0x2, 100000)) {' + TAB +
               '    panic ("NATIVE_UFS adopt: request list not ready hcs=%08x", read32 (hc, 0x30));' + TAB +
               '  }')
KEEPQ_START_FIX = ('  write32 (hc, 0x20, 0xffffffffU);  // clear stale status' + TAB +
                   '  if (keepQueue != 1) {   /* 2 = start the list, 0 = default, 1 = leave stopped */' + TAB +
                   '    write32 (hc, 0x60, 1);' + TAB +
                   '    if (!waitSet (hc, 0x30, 0x2, 100000)) {' + TAB +
                   '      panic ("NATIVE_UFS adopt: request list not ready hcs=%08x", read32 (hc, 0x30));' + TAB +
                   '    }' + TAB +
                   '  }')

STAGE_ANCHORS = [
    ('  gRootDiagnostic.stage = "SMMU and request list";',
     '  printf ("NATIVE_UFS bringup: dma=%llx stopping list' + NL + '", gHost.phys);' + TAB +
     '  gRootDiagnostic.stage = "SMMU and request list";'),
    ('  gHost.smmu = gHost.smmuMap->getVirtualAddress ();',
     '  gHost.smmu = gHost.smmuMap->getVirtualAddress ();' + TAB +
     '  printf ("NATIVE_UFS bringup: smmu bypass done' + NL + '");'),
    ('  if (!hostStartQueue (why)) return false;',
     '  printf ("NATIVE_UFS bringup: starting list' + NL + '");' + TAB +
     '  if (!hostStartQueue (why)) return false;' + TAB +
     '  printf ("NATIVE_UFS bringup: list running' + NL + '");'),
]

IRQ_ANCHOR = '  if (gIrqMode || !&armcpu_spi_attach || !&armcpu_spi_mask) return;'

# A statically linked driver only gets the personalities in its Info.plist, while
# start() gives the hardware work to the root-device personality.  Without it the
# ufshc nub match returns immediately and no probe ever runs.
PLIST = ROOT / 'drivers/ShengUfs/Info.plist'
PLIST_ANCHOR = '<key>IOProbeScore</key><integer>1000</integer>' + TAB + '  </dict></dict>'
PLIST_ROOT = ('<key>IOProbeScore</key><integer>1000</integer>' + TAB + '  </dict>' + TAB +
              '  <key>sheng SM8550 UFSHCI root</key><dict>' + TAB +
              '    <key>IOClass</key><string>NativeUfs</string>' + TAB +
              '    <key>IOProviderClass</key><string>IOPlatformExpertDevice</string>' + TAB +
              '    <key>IOMatchCategory</key><string>NativeUfsRoot</string>' + TAB +
              '    <key>IOProbeScore</key><integer>1000</integer>' + TAB +
              '  </dict></dict>')

if not SOURCE.is_file():
    raise SystemExit(f'patch-native-ufs-adopt-fix: missing {SOURCE}')

text = SOURCE.read_bytes().decode('utf-8', 'surrogateescape')
notes = []

if PLIST.is_file():
    ptext = PLIST.read_bytes().decode('utf-8', 'surrogateescape')
    if 'NativeUfsRoot' not in ptext and PLIST_ANCHOR in ptext:
        PLIST.write_bytes(ptext.replace(PLIST_ANCHOR, PLIST_ROOT, 1).encode('utf-8', 'surrogateescape'))
        notes.append('root personality')

if IRQ_ANCHOR in text:
    text = text.replace(IRQ_ANCHOR, '  return;   /* board: no ArmCpu SPI helpers; poll only */', 1)
    notes.append('irq mode off')

if PROD_CALL in text:
    text = text.replace(PROD_CALL, PROD_PLAIN, 1)
    notes.append('production S2CR bypass')

# A bring-up failure used to panic, which this kernel does not route into the
# capture buffer; print the reason first so the loader log records it.
FAIL_ANCHOR = '      panic ("NATIVE_UFS root check: bring-up failed: %s", why);'
FAIL_FIX = ('      printf ("NATIVE_UFS root check: bring-up failed: %s' + NL + '", why);' + TAB +
            '      panic ("NATIVE_UFS root check: bring-up failed: %s", why);')
if FAIL_ANCHOR in text:
    text = text.replace(FAIL_ANCHOR, FAIL_FIX, 1)
    notes.append('bring-up failure printed')

START_MARK = 'static bool hostBringUp (const char **why)' + NL + '{'
START_FIX = START_MARK + NL + '  printf ("NATIVE_UFS bringup: enter' + NL + '");'
if START_MARK in text and 'bringup: enter' not in text:
    text = text.replace(START_MARK, START_FIX, 1)
    notes.append('bring-up entry marker')

ENTRY_ANCHOR = 'bool NativeUfs::start (IOService *provider)' + TAB + '{'
ENTRY_FIX = (ENTRY_ANCHOR + TAB +
             '  printf ("NATIVE_UFS start provider=%s' + NL + '", provider ? provider->getName () : "null");')
BRING_ANCHOR = 'static bool hostBringUp (const char **why)' + TAB + '{'
BRING_FIX = (BRING_ANCHOR + TAB + '  printf ("NATIVE_UFS bringup: enter' + NL + '");')
MAP_ANCHOR = '  gHost.hc = mapPhys (UFS_HC_BASE, UFS_HC_SIZE, gHost.hcMap);'
MAP_FIX = (MAP_ANCHOR + TAB +
           '  printf ("NATIVE_UFS bringup: hc=%llx cap=%08x hcs=%08x hce=%08x' + NL + '",' + TAB +
           '          (unsigned long long)gHost.hc, read32 (gHost.hc, 0), read32 (gHost.hc, 0x30), read32 (gHost.hc, 0x34));')
ALLOC_ANCHOR = '    if (!dma) continue;'
ALLOC_FIX = ('    if (!dma) {' + TAB +
             '      printf ("NATIVE_UFS bringup: alloc %u bytes failed' + NL + '", (unsigned)bytes);' + TAB +
             '      continue;' + TAB +
             '    }' + TAB +
             '    printf ("NATIVE_UFS bringup: alloc %u bytes ok' + NL + '", (unsigned)bytes);')
SET_ANCHOR = '    gHost.dma = dma; gHost.virt = virt; gHost.phys = phys; gHost.dmaBytes = bytes;'
SET_FIX = (SET_ANCHOR + TAB +
           '    printf ("NATIVE_UFS bringup: dma phys=%llx bytes=%u' + NL + '",' + TAB +
           '            (unsigned long long)phys, (unsigned)bytes);')
# native-ufs-probe=20 hands the controller to the ported pandora driver
# (bsd/dev/sweet_ufs.c) instead of the ShengUfs takeover.
# 0022's probe: force the data-a-live stream state first (SMR[0] matches id 0x60,
# S2CR[0] bypasses), because this unit's bootloader left stream 0x60 unmatched.
SWEET_SMMU_ANCHOR = '	printf("NX741J UFS: probe start' + chr(92) + 'n");'
SWEET_SMMU_FIX = (SWEET_SMMU_ANCHOR + TAB +
                  '  {' + TAB +
                  '    uintptr_t sm = ml_io_map (0x15000000ULL, 0x1000);' + TAB +
                  '    if (sm) {' + TAB +
                  '      *(volatile uint32_t *)(sm + 0x800) = 0x80000060u;' + TAB +
                  '      *(volatile uint32_t *)(sm + 0xc00) = 1u;' + TAB +
                  '      __builtin_arm_dsb (0xf);' + TAB +
                  '      printf ("NX741J UFS: stream 0x60 smr=%08x s2cr=%08x' + chr(92) + 'n",' + TAB +
                  '              *(volatile uint32_t *)(sm + 0x800), *(volatile uint32_t *)(sm + 0xc00));' + TAB +
                  '    }' + TAB +
                  '  }')

PROBE37_ANCHOR = '  if (probe == 10) {'
PROBE37_FIX = ('  if (probe == 37) {' + TAB +
              '    printf ("NATIVE_UFS: TZ xPU probe' + chr(92) + 'n");' + TAB +
              '    native_ufs_probe_xpu ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE36_ANCHOR = '  if (probe == 10) {'
PROBE36_FIX = ('  if (probe == 36) {' + TAB +
              '    printf ("NATIVE_UFS: UFSHCI ordered bring-up' + chr(92) + 'n");' + TAB +
              '    native_ufs_probe_spec ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE35_ANCHOR = '  if (probe == 10) {'
PROBE35_FIX = ('  if (probe == 35) {' + TAB +
              '    printf ("NATIVE_UFS: ABL list region test' + NL + '");' + TAB +
              '    native_ufs_probe_abl_list ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE34_ANCHOR = '  if (probe == 10) {'
PROBE34_FIX = ('  if (probe == 34) {' + TAB +
              '    printf ("NATIVE_UFS: smmu walk (reference layout)' + NL + '");' + TAB +
              '    native_ufs_probe_smmu_walk ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE33_ANCHOR = '  if (probe == 10) {'
PROBE33_FIX = ('  if (probe == 33) {' + TAB +
              '    printf ("NATIVE_UFS: stream 0x60 bypass test' + NL + '");' + TAB +
              '    native_ufs_probe_bypass ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE32_ANCHOR = '  if (probe == 10) {'
PROBE32_FIX = ('  if (probe == 32) {' + TAB +
              '    printf ("NATIVE_UFS: CB0 page read' + NL + '");' + TAB +
              '    native_ufs_probe_cb ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE31_ANCHOR = '  if (probe == 10) {'
PROBE31_FIX = ('  if (probe == 31) {' + TAB +
              '    printf ("NATIVE_UFS: read-only SMMU layout dump' + NL + '");' + TAB +
              '    native_ufs_probe_dump ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE30_ANCHOR = '  if (probe == 10) {'
PROBE30_FIX = ('  if (probe == 30) {' + TAB +
              '    printf ("NATIVE_UFS: IOVA probe, stream 0x60' + NL + '");' + TAB +
              '    native_ufs_probe_iova ();' + TAB +
              '    return true;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
PROBE20_ANCHOR = '  if (probe == 10) {'
DECL_ANCHOR = 'volatile uint32_t native_ufs_probe_requested = 0;'
DECL_FIX = (DECL_ANCHOR + TAB + 'extern "C" int native_ufs_probe (void);' + TAB + 'extern "C" int native_ufs_probe_iova (void);' + TAB + 'extern "C" int native_ufs_probe_dump (void);' + TAB + 'extern "C" int native_ufs_probe_cb (void);' + TAB + 'extern "C" int native_ufs_probe_bypass (void);' + TAB + 'extern "C" int native_ufs_probe_smmu_walk (void);' + TAB + 'extern "C" int native_ufs_probe_abl_list (void);' + TAB + 'extern "C" int native_ufs_probe_spec (void);' + TAB + 'extern "C" int native_ufs_probe_xpu (void);')
PROBE20_FIX = ('  if (probe == 20) {' + TAB +
              '    printf ("NATIVE_UFS: handing over to sweet_ufs' + NL + '");' + TAB +
              '    int e20 = native_ufs_probe ();' + TAB +
              '    printf ("NATIVE_UFS: sweet_ufs rc=%d' + NL + '", e20);' + TAB +
              '    return e20 == 0;' + TAB +
              '  }' + TAB +
              '  if (probe == 10) {')
for anchor, replacement in ((DECL_ANCHOR, DECL_FIX), (ENTRY_ANCHOR, ENTRY_FIX), (BRING_ANCHOR, BRING_FIX), (MAP_ANCHOR, MAP_FIX), (ALLOC_ANCHOR, ALLOC_FIX), (SET_ANCHOR, SET_FIX), (SWEET_SMMU_ANCHOR, SWEET_SMMU_FIX), (PROBE37_ANCHOR, PROBE37_FIX), (PROBE36_ANCHOR, PROBE36_FIX), (PROBE35_ANCHOR, PROBE35_FIX), (PROBE34_ANCHOR, PROBE34_FIX), (PROBE33_ANCHOR, PROBE33_FIX), (PROBE32_ANCHOR, PROBE32_FIX), (PROBE31_ANCHOR, PROBE31_FIX), (PROBE30_ANCHOR, PROBE30_FIX), (PROBE20_ANCHOR, PROBE20_FIX)):
    if anchor in text and replacement not in text:
        text = text.replace(anchor, replacement, 1)
        notes.append('entry marker')

for anchor, replacement in ((KEEPQ_DECL, KEEPQ_DECL_FIX), (KEEPQ_STOP, KEEPQ_STOP_FIX), (KEEPQ_BASE, KEEPQ_BASE_FIX), (KEEPQ_START, KEEPQ_START_FIX)):
    if anchor in text:
        text = text.replace(anchor, replacement, 1)
        notes.append('keepqueue guard')

for anchor, replacement in STAGE_ANCHORS:
    if anchor in text and replacement not in text:
        text = text.replace(anchor, replacement, 1)
        notes.append('bringup marker')

start = text.find(HEAD)
if start < 0:
    raise SystemExit('patch-native-ufs-adopt-fix: adoptAndRead not found')
end = text.find(chr(10) + '}' + chr(10), start)
if end < 0:
    raise SystemExit('patch-native-ufs-adopt-fix: function end not found')
end += 3
body = text[start:end]
wide = body.count(WIDE) + body.count(WIDE_INV)
if wide:
    body = body.replace(WIDE, NARROW).replace(WIDE_INV, NARROW_INV)
    notes.append(f'narrowed {wide} clean(s)')
if SMMU_CALL in body:
    body = body.replace(SMMU_CALL, SMMU_PLAIN, 1)
    notes.append('adopt S2CR bypass')
if RING in body and 'ring-after' not in body:
    body = body.replace(RING, RING_PROBE, 1)
    notes.append('ring before/after probe')

if not notes:
    print('== patch-native-ufs-adopt-fix: already applied')
    raise SystemExit(0)
SOURCE.write_bytes((text[:start] + body + text[end:]).encode('utf-8', 'surrogateescape'))
print('== patch-native-ufs-adopt-fix: ' + ', '.join(notes))