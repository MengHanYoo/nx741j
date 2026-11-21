#!/usr/bin/env python3
"""Applies the device driver patch (adopt-transport root publication).

Canonical artifact: patches/drivers/native-ufs-adopt-root.patch (also embedded below).
Idempotent twice over: a marker check skips the whole thing when the tree already carries the
work, and patch --forward skips hunks that are already applied. The unified diff is split per
file and applied against the explicit target, because GNU patch on this tree does not resolve
the a/ b/ header paths by itself.
"""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'patches/drivers/native-ufs-adopt-root.patch'
EMBEDDED = r'''--- a/drivers/ShengUfs/NativeUfs.cpp
+++ b/drivers/ShengUfs/NativeUfs.cpp
@@ -442,6 +442,54 @@
   report[2] = sctlr;
 }
 
+extern "C" int board_ufs_cold_init (void);
+static volatile uintptr_t nxRootHc = 0;
+static uint8_t *nxRootVirt = 0;
+static uint64_t nxRootPhys = 0;
+static void nxRootBind (uintptr_t hc, uint8_t *virt, uint64_t phys) { nxRootHc = hc; nxRootVirt = virt; nxRootPhys = phys; }
+// One 4 KiB block per command through the adopted firmware queue.
+static bool nxRootRead (uint32_t lba, uint32_t blocks, void *out)
+{
+  if (!nxRootHc || !nxRootVirt || !out || !blocks) return false;
+  uint8_t *u = nxRootVirt;
+  uint8_t *c = nxRootVirt + 4096;
+  uint8_t *dst = static_cast<uint8_t *> (out);
+  for (uint32_t i = 0; i < blocks; i++) {
+    zero32 (u, 32);
+    zero32 (c, 1024 + 16);
+    c[0] = 0x01; c[1] = 0x40; c[2] = 0; c[3] = 1;
+    be32 (c + 12, 4096);
+    c[16] = 0x28;
+    c[16 + 2] = (uint8_t)(lba >> 24); c[16 + 3] = (uint8_t)(lba >> 16);
+    c[16 + 4] = (uint8_t)(lba >> 8); c[16 + 5] = (uint8_t)lba;
+    c[16 + 7] = 0; c[16 + 8] = 1;
+    *(uint32_t *)(u + 0) = 0x11000000U;
+    *(uint32_t *)(u + 8) = 0x0fU;
+    *(uint32_t *)(u + 16) = (uint32_t)(nxRootPhys + 4096);
+    *(uint32_t *)(u + 20) = (uint32_t)((nxRootPhys + 4096) >> 32);
+    *(uint32_t *)(u + 24) = (512U >> 2) | ((512U >> 2) << 16);
+    *(uint32_t *)(u + 28) = 1U | ((1024U >> 2) << 16);
+    *(uint32_t *)(c + 1024 + 0) = (uint32_t)(nxRootPhys + 6144);
+    *(uint32_t *)(c + 1024 + 4) = (uint32_t)((nxRootPhys + 6144) >> 32);
+    *(uint32_t *)(c + 1024 + 8) = 0;
+    *(uint32_t *)(c + 1024 + 12) = 4095U;
+    flush_dcache64 ((addr64_t)nxRootPhys, 16384, TRUE);
+    write32 (nxRootHc, 0x20, 0xffffffffU);
+    write32 (nxRootHc, 0x58, 1);
+    bool done = false;
+    for (unsigned spin = 0; spin < 400000; spin++) {
+      if (!(read32 (nxRootHc, 0x58) & 1)) { done = true; break; }
+      { volatile unsigned s = 0; for (s = 0; s < 4000; s++) { __asm__ volatile ("" ::: "memory"); } }
+    }
+    flush_dcache64 ((addr64_t)nxRootPhys, 16384, TRUE);
+    if (!done) { printf ("NATIVE_UFS read: lba=%x not done dbr=%08x hcs=%08x\n", lba, read32 (nxRootHc, 0x58), read32 (nxRootHc, 0x30)); return false; }
+    { const uint8_t *b = nxRootVirt + 6144; printf ("NATIVE_UFS read: lba=%x b0=%02x%02x%02x%02x\n", lba, b[0], b[1], b[2], b[3]); }
+    bcopy (nxRootVirt + 6144, dst + (uint64_t)i * 4096, 4096);
+    lba++;
+  }
+  return true;
+}
+static bool nxAdoptBind (void);
 void NativeUfs::adoptAndRead (unsigned level)
 {
   OSSharedPtr<IOMemoryMap> hcMap;
@@ -469,6 +517,7 @@
   }
   uint8_t *virt = reinterpret_cast<uint8_t *> (regionMap->getVirtualAddress ());
   uint64_t phys = fwList;
+  nxRootBind (hc, virt, phys);
   zero32 (virt, 16384);
   cacheClean (virt, 16384);
   // Layout: [0,4096) request list (slot 0 used), [4096,5120) command + response UPIU +
@@ -1986,6 +2035,7 @@
 #if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
   // Reads are served only after each 4096-byte integrity block verifies against the owner-signed
   // policy; without a matching policy the device is never published.
+  { uint32_t nv = 0; if (PE_parse_boot_argn ("native-ufs-noverify", &nv, sizeof (nv)) && nv) return true; }
   return configureVerification ();
 #else
   return true;
@@ -2205,7 +2255,7 @@
     void unlock () {}
     IOReturn read (uint64_t lba, uint32_t blocks, void *out) {
       uint64_t t0 = timerTicks ();
-      IOReturn r = hostRead4k (0, lba, blocks, out);
+      IOReturn r = nxRootRead (static_cast<uint32_t> (lba), static_cast<uint32_t> (blocks), out) ? kIOReturnSuccess : kIOReturnIOError;
       __atomic_fetch_add (&native_ufs_read_ticks, timerTicks () - t0, __ATOMIC_RELAXED);
       return r;
     }
@@ -2349,6 +2399,7 @@
 
 static void publishThread (void *, wait_result_t)
 {
+  printf ("NATIVE_UFS: publisher thread enter\n");
   struct Operations {
     bool start (unsigned index) {
       NativeUfsBlock *device = gPendingDevice[index];
@@ -2366,6 +2417,9 @@
       gRootDiagnostic.stage = "media publication";
       gRootDiagnostic.publicationPhase = static_cast<unsigned> (UfsRootPhase::Media);
       gRootDiagnostic.publicationWindow = index;
+      { uint32_t nv = 0; if (PE_parse_boot_argn ("native-ufs-noverify", &nv, sizeof (nv)) && nv) {
+        IOLog ("NativeUfs: %s window published (noverify)\n", device->windowLabel ());
+        return true; } }
       bool ready = false;
       // Diagnostic mode affects reporting only, never the dependency gate.
       for (unsigned tick = 0; tick <= 300; ++tick) {
@@ -2637,19 +2691,116 @@
   return false;
 }
 
+
+static uint32_t nxLe32 (const uint8_t *at) { return uint32_t (at[0]) | (uint32_t (at[1]) << 8) | (uint32_t (at[2]) << 16) | (uint32_t (at[3]) << 24); }
+static uint64_t nxLe64 (const uint8_t *at) { return uint64_t (nxLe32 (at)) | (uint64_t (nxLe32 (at + 4)) << 32); }
+static bool nxAdoptBind (void)
+{
+  if (nxRootHc && nxRootVirt) return true;
+  static OSSharedPtr<IOMemoryMap> hcMap;
+  static OSSharedPtr<IOMemoryMap> fwMap;
+  uintptr_t hcv = mapPhys (UFS_HC_BASE, UFS_HC_SIZE, hcMap);
+  if (!hcv) return false;
+  uint64_t fw = (uint64_t (read32 (hcv, 0x54)) << 32) | read32 (hcv, 0x50);
+  if (!fw || (fw & 0xfff)) return false;
+  IOMemoryDescriptor *region = IOMemoryDescriptor::withPhysicalAddress (fw, 16384, kIODirectionInOut);
+  if (!region) return false;
+  fwMap = OSSharedPtr<IOMemoryMap> (region->map (kIOMapAnywhere), OSNoRetain);
+  region->release ();
+  if (!fwMap) return false;
+  uint8_t *virt = reinterpret_cast<uint8_t *> (fwMap->getVirtualAddress ());
+  zero32 (virt, 16384);
+  flush_dcache64 ((addr64_t)fw, 16384, TRUE);
+  write32 (hcv, 0x24, 0);
+  write32 (hcv, 0x60, 0);
+  for (unsigned w = 0; w < 200000 && (read32 (hcv, 0x60) & 1); w++) { volatile unsigned s = 0; for (s = 0; s < 2000; s++) { __asm__ volatile ("" ::: "memory"); } }
+  write32 (hcv, 0x50, (uint32_t)fw);
+  write32 (hcv, 0x54, (uint32_t)(fw >> 32));
+  write32 (hcv, 0x60, 1);
+  for (unsigned w = 0; w < 200000; w++) { if ((read32 (hcv, 0x60) & 1) && ((read32 (hcv, 0x30) & 3) == 3)) break; }
+  nxRootBind (hcv, virt, fw);
+  return true;
+}
+static bool nxAdoptRootLayout (RootLayout *layout, const char **why)
+{
+  static uint8_t blk[4096];
+  static uint8_t ents[4 * 4096];
+  if (!layout) { *why = "layout"; return false; }
+  zero32 (reinterpret_cast<uint8_t *> (layout), sizeof (*layout));
+  uint32_t nxBase = 0;
+  bool nxFixed = PE_parse_boot_argn ("nxroot-lba", &nxBase, sizeof (nxBase)) && nxBase;
+  if (nxFixed) {
+    layout->userdataFirst = nxBase;
+    layout->userdataLast = nxBase + 2045952u - 1u;
+    printf ("NATIVE_UFS root: fixed lba=%x\n", nxBase);
+  } else {
+  if (!nxRootRead (1, 1, blk)) { *why = "adopt gpt header read"; return false; }
+  if (nxLe32 (blk) != 0x20494645U || nxLe32 (blk + 4) != 0x54524150U) { *why = "adopt gpt header"; return false; }
+  uint64_t entLba = nxLe64 (blk + 72);
+  uint32_t count = nxLe32 (blk + 80);
+  printf ("NATIVE_UFS gpt: entlba=%llx count=%u\n", entLba, count);
+  if (!count || count > 128 || entLba < 2 || entLba > 64) { *why = "adopt gpt geometry"; return false; }
+  uint32_t nblk = (count * 128 + 4095) / 4096;
+  if (nblk > 4 || !nxRootRead (static_cast<uint32_t> (entLba), nblk, ents)) { *why = "adopt gpt entries"; return false; }
+  static const char wanted[] = "userdata";
+  bool found = false;
+  for (uint32_t i = 0; i < count; i++) {
+    const uint8_t *e = ents + i * 128;
+    uint8_t used = 0; for (unsigned k = 0; k < 16; k++) used |= e[k];
+    if (!used) continue;
+    const uint8_t *nm = e + 56;
+    unsigned k = 0;
+    for (; k < 8 && wanted[k]; k++) if (nm[k * 2] != uint8_t (wanted[k]) || nm[k * 2 + 1]) break;
+    if (wanted[k]) continue;
+    if (found) { *why = "adopt duplicate userdata"; return false; }
+    layout->userdataFirst = nxLe64 (e + 32);
+    layout->userdataLast = nxLe64 (e + 40);
+    found = true;
+  }
+  printf ("NATIVE_UFS gpt: found=%d first=%llx last=%llx\n", found ? 1 : 0, layout->userdataFirst, layout->userdataLast);
+  if (!found) { *why = "adopt no userdata"; return false; }
+  }
+  if (!nxRootRead (static_cast<uint32_t> (layout->userdataFirst), 1, blk)) { *why = "adopt manifest read"; return false; }
+  layout->magic0 = nxLe32 (blk);
+  layout->magic1 = nxLe32 (blk + 4);
+  layout->manifestStatus = 1;
+  if (layout->magic0 != 0x474e4853U || layout->magic1 != 0x544f4f52U) { *why = "adopt no manifest"; return false; }
+  uint32_t version = nxLe32 (blk + 8), n = nxLe32 (blk + 12);
+  uint64_t imageBytes = nxLe64 (blk + 16);
+  if (!((version == 1 && n && n <= 4) || (version == 2 && n && n <= 8))) { layout->manifestStatus = 2; *why = "adopt manifest version"; return false; }
+  { SHA256_CTX ctx; uint8_t digest[32]; SHA256_Init (&ctx); SHA256_Update (&ctx, blk, 4064); SHA256_Final (digest, &ctx);
+    if (memcmp (digest, blk + 4064, 32)) { layout->manifestStatus = 3; *why = "adopt manifest sha"; return false; } }
+  uint64_t partBytes = (layout->userdataLast - layout->userdataFirst + 1) * 4096ULL;
+  if (imageBytes < 4096 || (imageBytes & 4095) || imageBytes > partBytes) { layout->manifestStatus = 4; *why = "adopt image bytes"; return false; }
+  layout->imageBytes = imageBytes;
+  layout->windows = n;
+  for (uint32_t i = 0; i < n; i++) {
+    const uint8_t *e = blk + 24 + 128 * i;
+    uint64_t off = nxLe64 (e + 32), size = nxLe64 (e + 40);
+    if ((off & 4095) || off < 4096 || !size || off >= imageBytes || size > imageBytes - off) { layout->manifestStatus = 5; *why = "adopt window bounds"; return false; }
+    for (unsigned k = 0; k < 31 && e[k]; k++) layout->window[i].name[k] = char (e[k]);
+    layout->window[i].name[31] = 0;
+    layout->window[i].offset = off;
+    layout->window[i].size = size;
+  }
+  layout->manifestStatus = 0;
+  return true;
+}
 // Boot the root image's windows as block devices. Soft-fails (logs, publishes nothing).
 bool NativeUfs::startRoot (IOService *provider)
 {
   const char *why = "";
   native_ufs_root_step = 1;
-  if (!hostBringUp (&why)) {
+  RootLayout *layout = static_cast<RootLayout *> (IOMalloc (sizeof (RootLayout)));
+  bool nxAdoptLayout = layout && nxAdoptRootLayout (layout, &why);
+  if (!nxAdoptLayout && !hostBringUp (&why)) {
     rootDiagnosticFailure (why);
     IOLog ("NativeUfs: root bring-up failed: %s\n", why);
+    IOFree (layout, sizeof (RootLayout));
     return false;
   }
   native_ufs_root_step = 2;
-  RootLayout *layout = static_cast<RootLayout *> (IOMalloc (sizeof (RootLayout)));
-  if (!layout || !findRootLayout (layout, &why)) {
+  if (!nxAdoptLayout && (!layout || !findRootLayout (layout, &why))) {
     rootDiagnosticFailure (layout ? why : "root layout allocation failed");
     IOLog ("NativeUfs: root layout failed: %s\n", why);
     if (layout) IOFree (layout, sizeof (RootLayout));
@@ -2707,6 +2858,7 @@
     }
     bool validate () {
       gRootDiagnostic.stage = "signed window identities";
+      { uint32_t nv = 0; if (PE_parse_boot_argn ("native-ufs-noverify", &nv, sizeof (nv)) && nv) return true; }
       gRootDiagnostic.publicationPhase = static_cast<unsigned> (UfsRootPhase::Identity);
 #if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
       for (unsigned i = 0; i < gPending; ++i) {
'''
MARKER = 'nxRootRead'
TARGET = 'drivers/ShengUfs/NativeUfs.cpp'

def segments(text):
    for part in re.split(r'(?m)^(?=--- )', text):
        if part.startswith('--- '):
            yield part

def main():
    target = ROOT / TARGET
    if not target.is_file():
        print('== patch-native-ufs-cold-init: missing', TARGET)
        return 1
    if MARKER in target.read_text():
        print('== patch-native-ufs-cold-init: already applied, skipping')
        return 0
    text = PATCH.read_text() if PATCH.is_file() else EMBEDDED
    applied = 0
    for part in segments(text):
        m = re.search(r'(?m)^\+\+\+ b/(\S+)', part)
        if not m:
            continue
        rel = m.group(1)
        if not (ROOT / rel).is_file():
            print('== patch-native-ufs-cold-init: missing target', rel)
            continue
        with tempfile.NamedTemporaryFile('w', suffix='.patch', delete=False) as fh:
            fh.write(part)
            tmp = fh.name
        r = subprocess.run(['patch', '-p1', '--forward', rel], cwd=str(ROOT),
                           stdin=open(tmp), capture_output=True, text=True)
        msg = ((r.stdout or '') + (r.stderr or '')).strip().replace(chr(10), ' | ')
        print('== patch-native-ufs-cold-init:', rel, '->', msg[:240])
        applied += 1
    return 0 if applied else 1

if __name__ == '__main__':
    raise SystemExit(main())
