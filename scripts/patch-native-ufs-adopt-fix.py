#!/usr/bin/env python3
"""Adopt-path fixes plus a doorbell probe for the canoe UFS host.

1) SMMU: the production takeover needs (pages+banks)*4096 <= 0xD0000; this unit
   reports IDR1=0x60000051 (pages=128, banks=81 -> 0xD1000), so reading SCTLR
   faults.  Stream 0x60 has no match entry (SMR=0, S2CR=0) and SMMU_SCR0 bit0=0
   (SMMU disabled), so write S2CR[0x60]=1 and move on.
2) The adopt path cleaned 32 KiB out of a 16 KiB mapping; narrow to the window.
3) Ring probe: panic immediately before the doorbell write so the register state
   at that exact moment reaches the console (and therefore the loader log).

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

SMMU_CALL = '  smmuBypassStream (0x60, smmuMap, smmuReport, phys, 16384);'
SMMU_PLAIN = """  /*
   * IDR1=0x60000051 on this unit: pages=128, banks=81, so the context-bank window
   * would need 0xD1000 while the mapping is 0xD0000.  Stream 0x60 has no match
   * entry (SMR=0, S2CR=0), so bypass it directly instead of planning a bank.
   */
  {
    uintptr_t smmu = mapPhys (0x15000000ULL, 0xd0000, smmuMap);
    if (!smmu) { panic ("NATIVE_UFS smmu: map failed"); }
    write32 (smmu, 0xc00 + 4 * 0x60, 1U);        /* S2CR[0x60] TYPE=bypass */
    __asm__ volatile ("dsb sy" ::: "memory");
    smmuReport[0] = 0;
    smmuReport[1] = read32 (smmu, 0xc00 + 4 * 0x60);
    smmuReport[2] = read32 (smmu, 0x24);
  }"""

RING = '    write32 (hc, 0x58, 1);          // ring slot 0'
RING_PROBE = r"""    panic ("NATIVE_UFS ring-before\n"
           "hcs=%08x rsr=%08x dbr=%08x is=%08x utrlba=%08x utrlrsr=%08x\n"
           "utrd0=%08x utrd2=%08x utrd4=%08x ie=%08x",
           read32 (hc, 0x30), read32 (hc, 0x60), read32 (hc, 0x58), read32 (hc, 0x20),
           read32 (hc, 0x50), read32 (hc, 0x60),
           *reinterpret_cast<volatile uint32_t *> (utrd + 0),
           *reinterpret_cast<volatile uint32_t *> (utrd + 8),
           *reinterpret_cast<volatile uint32_t *> (utrd + 16),
           read32 (hc, 0x24));
""" + RING

if not SOURCE.is_file():
    raise SystemExit(f'patch-native-ufs-adopt-fix: missing {SOURCE}')

text = SOURCE.read_bytes().decode('utf-8', 'surrogateescape')
start = text.find(HEAD)
if start < 0:
    raise SystemExit('patch-native-ufs-adopt-fix: adoptAndRead not found')
end = text.find(chr(10) + '}' + chr(10), start)
if end < 0:
    raise SystemExit('patch-native-ufs-adopt-fix: function end not found')
end += 3
body = text[start:end]

notes = []
wide = body.count(WIDE) + body.count(WIDE_INV)
if wide:
    body = body.replace(WIDE, NARROW).replace(WIDE_INV, NARROW_INV)
    notes.append(f'narrowed {wide} clean(s)')
if SMMU_CALL in body:
    body = body.replace(SMMU_CALL, SMMU_PLAIN, 1)
    notes.append('S2CR bypass')
if RING in body and 'ring-before' not in body:
    body = body.replace(RING, RING_PROBE, 1)
    notes.append('ring-before probe')

if not notes:
    print('== patch-native-ufs-adopt-fix: already applied')
    raise SystemExit(0)
SOURCE.write_bytes((text[:start] + body + text[end:]).encode('utf-8', 'surrogateescape'))
print('== patch-native-ufs-adopt-fix: ' + ', '.join(notes))
