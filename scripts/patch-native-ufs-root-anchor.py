#!/usr/bin/env python3
"""Teach ShengUfs to locate a relocated SHNGROOT image without nxroot-lba.

Run after patch-native-ufs-adopt-fix.py and before component:ShengUfs.  The
patch is tied to the pinned fd420df source and refuses an unknown revision.
"""
from __future__ import annotations

import os
from pathlib import Path
import sys

ROOT = Path(os.environ.get("UNIXNU_UFS_ROOT", Path(__file__).resolve().parents[1]))
NATIVE = next((path for path in (
    ROOT / "drivers/ShengUfs/NativeUfs.cpp", ROOT / "NativeUfs.cpp"
) if path.is_file()), ROOT / "drivers/ShengUfs/NativeUfs.cpp")
LAYOUT = next((path for path in (
    ROOT / "drivers/ShengUfs/RootLayout.h", ROOT / "RootLayout.h"
) if path.is_file()), ROOT / "drivers/ShengUfs/RootLayout.h")
MARKER = "NXROOTP1"


HELPERS = r'''
// A tail anchor is written in the partition's final 4096-byte block.  It is
// untrusted input: every field is range-checked and the first 96 bytes carry
// their own SHA-256 before the anchor can select a SHNGROOT block.
struct RootAnchorInfo {
  uint64_t manifestLba, imageBytes, imageSectors;
  uint8_t manifestSha256[32];
};

static bool rootAnchorFromBlock (const uint8_t *block, uint64_t partitionFirst,
                                 uint64_t partitionLast, RootAnchorInfo *info, const char **why)
{
  if (memcmp (block, "NXROOTP1", 8) || le32 (block + 8) != 1 || le32 (block + 12) != 0) {
    *why = "root anchor format";
    return false;
  }
  SHA256_CTX context;
  uint8_t digest[32];
  SHA256_Init (&context);
  SHA256_Update (&context, block, 96);
  SHA256_Final (digest, &context);
  if (memcmp (digest, block + 96, 32)) {
    *why = "root anchor checksum";
    return false;
  }
  uint64_t first = le64 (block + 16), last = le64 (block + 24);
  uint64_t manifest = le64 (block + 32), bytes = le64 (block + 40);
  uint64_t sectors = le64 (block + 48);
  if (first != partitionFirst || last != partitionLast || last < first ||
      !bytes || (bytes & 4095) || sectors != (bytes + 4095) / 4096 ||
      manifest < first || manifest > last || sectors > last - manifest + 1) {
    *why = "root anchor bounds";
    return false;
  }
  info->manifestLba = manifest;
  info->imageBytes = bytes;
  info->imageSectors = sectors;
  memcpy (info->manifestSha256, block + 56, 32);
  return true;
}

// Parse one SHNGROOT block.  imageLba names the first block of the root image;
// userdataFirst/userdataLast remain the real partition bounds used by Data and
// the diagnostic log.
static bool rootManifestFromBlock (RootLayout *layout, const uint8_t *block, const char **why)
{
  layout->magic0 = le32 (block);
  layout->magic1 = le32 (block + 4);
  layout->manifestStatus = 1;
  if (layout->magic0 != 0x474e4853U || layout->magic1 != 0x544f4f52U) {
    *why = "no SHNGROOT manifest";
    return false;
  }
  uint32_t version = le32 (block + 8), n = le32 (block + 12);
  uint64_t imageBytes = le64 (block + 16);
  layout->manifestStatus = 2;
  if (!((version == 1 && n && n <= 4) || (version == 2 && n && n <= 8))) {
    *why = "SHNGROOT version/count";
    return false;
  }
  SHA256_CTX context;
  uint8_t digest[32];
  SHA256_Init (&context);
  SHA256_Update (&context, block, 4064);
  SHA256_Final (digest, &context);
  layout->manifestStatus = 3;
  if (memcmp (digest, block + 4064, 32)) {
    *why = "SHNGROOT checksum";
    return false;
  }
  if (!imageBytes || imageBytes > UINT64_MAX - 4095 ||
      layout->imageLba < layout->userdataFirst || layout->imageLba > layout->userdataLast) {
    layout->manifestStatus = 4;
    *why = "SHNGROOT image bounds";
    return false;
  }
  uint64_t imageSectors = (imageBytes + 4095) / 4096;
  if (imageSectors > layout->userdataLast - layout->imageLba + 1) {
    layout->manifestStatus = 4;
    *why = "SHNGROOT exceeds partition";
    return false;
  }
  layout->windows = layout->objects = 0;
  bool bounds = true;
  for (uint32_t index = 0; index < n && bounds; ++index) {
    const uint8_t *entry = block + 24 + 128 * index;
    uint64_t offset = le64 (entry + 32), size = le64 (entry + 40);
    const bool isWindow = le32 (entry + 48) == 512 && le32 (entry + 52) == 1;
    const bool isObject = version == 2 && le32 (entry + 48) == 4096 && le32 (entry + 52) == 3;
    if (!isWindow && !isObject) {
      layout->manifestStatus = 7;
      *why = "SHNGROOT entry attributes";
      bounds = false;
      break;
    }
    if ((isWindow && layout->windows == 4) || (isObject && layout->objects == 4) ||
        (offset & 4095) || offset < 4096 || !size || offset >= imageBytes ||
        size > imageBytes - offset || size > UINT64_MAX - 4095 ||
        ((size + 4095) & ~4095ULL) > imageBytes - offset) {
      layout->manifestStatus = 4;
      *why = "SHNGROOT entry bounds";
      bounds = false;
      break;
    }
    unsigned length = 0;
    while (length < 32 && entry[length] >= 0x20 && entry[length] <= 0x7e) ++length;
    if (!length || length == 32 || entry[length] != 0) {
      layout->manifestStatus = 5;
      *why = "SHNGROOT entry name";
      bounds = false;
      break;
    }
    for (uint32_t prior = 0; prior < layout->windows + layout->objects; ++prior) {
      const RootWindow &other = prior < layout->windows ? layout->window[prior] : layout->object[prior - layout->windows];
      if (!strcmp (reinterpret_cast<const char *> (entry), other.name)) {
        layout->manifestStatus = 5;
        *why = "duplicate SHNGROOT entry";
        bounds = false;
        break;
      }
      if (offset < other.offset + ((other.size + 4095) & ~4095ULL) &&
          other.offset < offset + ((size + 4095) & ~4095ULL)) {
        layout->manifestStatus = 6;
        *why = "overlapping SHNGROOT entries";
        bounds = false;
        break;
      }
    }
    if (!bounds) break;
    RootWindow &slot = isWindow ? layout->window[layout->windows++] : layout->object[layout->objects++];
    memcpy (slot.name, entry, 32);
    slot.name[31] = 0;
    slot.offset = offset;
    slot.size = size;
    memcpy (slot.sha256, entry + 56, 32);
  }
  if (bounds && !layout->windows) {
    layout->manifestStatus = 7;
    *why = "SHNGROOT has no windows";
    bounds = false;
  }
  if (!bounds) {
    layout->windows = layout->objects = 0;
    return false;
  }
  layout->imageBytes = imageBytes;
  layout->manifestStatus = 0;
  return true;
}

'''


FIND_ROOT = r'''static bool findRootLayout (RootLayout *layout, const char **why)
{
  gRootDiagnostic.stage = "GPT header";
  bzero (layout, sizeof (*layout));
  if (hostRead4k (0, 1, 1, gScratch) != kIOReturnSuccess) {
    *why = "GPT header read";
    return false;
  }
  if (le32 (gScratch) != 0x20494645U || le32 (gScratch + 4) != 0x54524150U || le32 (gScratch + 84) != 128) {
    *why = "GPT header";
    return false;
  }
  uint32_t headerBytes = le32 (gScratch + 12);
  if (le32 (gScratch + 8) != 0x00010000 || headerBytes < 92 || headerBytes > 4096 ||
      le32 (gScratch + 20) || le64 (gScratch + 24) != 1 ||
      rootCrc32 (gScratch, headerBytes, true) != le32 (gScratch + 16)) {
    *why = "GPT header CRC/revision";
    return false;
  }
  uint64_t backup = le64 (gScratch + 32), first = le64 (gScratch + 40), last = le64 (gScratch + 48);
  uint32_t entriesCrc = le32 (gScratch + 88);
  uint64_t entriesLba = le64 (gScratch + 72);
  uint32_t count = le32 (gScratch + 80);
  layout->gptHeaderCrc = le32 (gScratch + 16);
  layout->gptEntriesCrc = entriesCrc;
  layout->gptEntriesLba = entriesLba;
  layout->gptEntryCount = count;
  if (entriesLba < 2 || entriesLba > 64 || !count || count > 128) {
    *why = "GPT geometry";
    return false;
  }
  uint32_t blocks = (count * 128 + 4095) / 4096;
  if (first < entriesLba + blocks || last < first || backup <= last || backup > UINT32_MAX ||
      backup - last <= blocks) {
    *why = "GPT usable range";
    return false;
  }
  gRootDiagnostic.stage = "GPT entries";
  if (hostRead4k (0, entriesLba, blocks, gScratch) != kIOReturnSuccess) {
    *why = "GPT entries read";
    return false;
  }
  if (rootCrc32 (gScratch, count * 128) != entriesCrc) {
    *why = "GPT entries CRC";
    return false;
  }
  static const char wanted[] = "userdata";
  bool found = false;
  for (uint32_t index = 0; index < count; ++index) {
    const uint8_t *entry = gScratch + index * 128;
    if (!rootEntryUsed (entry)) continue;
    uint64_t entryFirst = le64 (entry + 32), entryLast = le64 (entry + 40);
    if (entryFirst < first || entryLast < entryFirst || entryLast > last) {
      *why = "GPT partition bounds";
      return false;
    }
    for (uint32_t prior = 0; prior < index; ++prior) {
      const uint8_t *other = gScratch + prior * 128;
      if (rootEntryUsed (other) && entryFirst <= le64 (other + 40) && le64 (other + 32) <= entryLast) {
        *why = "GPT partition overlap";
        return false;
      }
    }
    bool match = true;
    for (unsigned c = 0; c < sizeof (wanted); ++c) {
      if (entry[56 + 2 * c] != static_cast<uint8_t> (wanted[c]) || entry[57 + 2 * c] != 0) {
        match = false;
        break;
      }
    }
    if (match) {
      if (found) {
        *why = "duplicate userdata partition";
        return false;
      }
      layout->userdataFirst = le64 (entry + 32);
      layout->userdataLast = le64 (entry + 40);
      found = true;
    }
  }
  if (!found || layout->userdataLast < layout->userdataFirst) {
    *why = "no userdata partition";
    return false;
  }
  gRootDiagnostic.first = layout->userdataFirst;
  gRootDiagnostic.last = layout->userdataLast;
  gRootDiagnostic.stage = "root manifest";
  if (hostRead4k (0, layout->userdataFirst, 1, gScratch) != kIOReturnSuccess) {
    *why = "userdata first block read";
    return false;
  }
  layout->magic0 = le32 (gScratch);
  layout->magic1 = le32 (gScratch + 4);
  layout->ext4Magic = uint32_t (gScratch[1080]) | (uint32_t (gScratch[1081]) << 8);
  layout->ext4Blocks = le32 (gScratch + 1028);
  if (layout->magic0 == 0x474e4853U && layout->magic1 == 0x544f4f52U) {
    layout->imageLba = layout->userdataFirst;
    rootManifestFromBlock (layout, gScratch, why);
  } else {
    gRootDiagnostic.stage = "root tail anchor";
    if (hostRead4k (0, layout->userdataLast, 1, gScratch) != kIOReturnSuccess) {
      layout->manifestStatus = 8;
      *why = "root anchor read";
    } else {
      RootAnchorInfo anchor = {};
      if (!rootAnchorFromBlock (gScratch, layout->userdataFirst, layout->userdataLast, &anchor, why)) {
        layout->manifestStatus = 8;
      } else if (anchor.manifestLba > UINT32_MAX ||
                 hostRead4k (0, anchor.manifestLba, 1, gScratch) != kIOReturnSuccess) {
        layout->manifestStatus = 8;
        *why = "anchored manifest read";
      } else {
        SHA256_CTX context;
        uint8_t digest[32];
        SHA256_Init (&context);
        SHA256_Update (&context, gScratch, 4096);
        SHA256_Final (digest, &context);
        layout->imageLba = anchor.manifestLba;
        if (memcmp (digest, anchor.manifestSha256, 32)) {
          layout->manifestStatus = 9;
          *why = "anchored manifest checksum";
        } else {
          rootManifestFromBlock (layout, gScratch, why);
        }
      }
    }
  }
  gRootDiagnostic.magic0 = layout->magic0;
  gRootDiagnostic.magic1 = layout->magic1;
  gRootDiagnostic.manifest = layout->manifestStatus;
  gRootDiagnostic.windows = layout->windows;
  return true;
}
'''


ADOPT_ROOT = r'''// Root image selection uses the SHNGROOT parser and the NXROOTP1 tail anchor.
static bool nxAdoptRootLayout (RootLayout *layout, const char **why)
{
  static uint8_t blk[4096];
  static uint8_t ents[4 * 4096];
  if (!layout) {
    *why = "layout";
    return false;
  }
  zero32 (reinterpret_cast<uint8_t *> (layout), sizeof (*layout));
  if (!nxRootRead (1, 1, blk) || nxLe32 (blk) != 0x20494645U || nxLe32 (blk + 4) != 0x54524150U) {
    *why = "adopt GPT header";
    return false;
  }
  uint64_t entriesLba = nxLe64 (blk + 72);
  uint32_t count = nxLe32 (blk + 80);
  if (!count || count > 128 || entriesLba < 2 || entriesLba > 64) {
    *why = "adopt GPT geometry";
    return false;
  }
  uint32_t entryBlocks = (count * 128 + 4095) / 4096;
  if (entryBlocks > 4 || !nxRootRead (static_cast<uint32_t> (entriesLba), entryBlocks, ents)) {
    *why = "adopt GPT entries";
    return false;
  }
  static const char wanted[] = "userdata";
  bool found = false;
  for (uint32_t index = 0; index < count; ++index) {
    const uint8_t *entry = ents + index * 128;
    uint8_t used = 0;
    for (unsigned byte = 0; byte < 16; ++byte) used |= entry[byte];
    if (!used) continue;
    bool match = true;
    for (unsigned byte = 0; byte < sizeof (wanted); ++byte) {
      if (entry[56 + byte * 2] != static_cast<uint8_t> (wanted[byte]) || entry[57 + byte * 2]) {
        match = false;
        break;
      }
    }
    if (!match) continue;
    if (found) {
      *why = "adopt duplicate userdata";
      return false;
    }
    layout->userdataFirst = nxLe64 (entry + 32);
    layout->userdataLast = nxLe64 (entry + 40);
    found = true;
  }
  if (!found || layout->userdataLast < layout->userdataFirst || layout->userdataLast > UINT32_MAX) {
    *why = "adopt no userdata";
    return false;
  }
  printf ("NATIVE_UFS gpt: first=%llx last=%llx\n",
          layout->userdataFirst, layout->userdataLast);
  if (!nxRootRead (static_cast<uint32_t> (layout->userdataFirst), 1, blk)) {
    *why = "adopt first block read";
    return false;
  }
  layout->magic0 = nxLe32 (blk);
  layout->magic1 = nxLe32 (blk + 4);
  if (layout->magic0 == 0x474e4853U && layout->magic1 == 0x544f4f52U) {
    layout->imageLba = layout->userdataFirst;
    printf ("NATIVE_UFS root: manifest at partition start\n");
  } else {
    if (!nxRootRead (static_cast<uint32_t> (layout->userdataLast), 1, blk)) {
      *why = "adopt root anchor read";
      return false;
    }
    RootAnchorInfo anchor = {};
    if (!rootAnchorFromBlock (blk, layout->userdataFirst, layout->userdataLast, &anchor, why)) {
      return false;
    }
    layout->imageLba = anchor.manifestLba;
    printf ("NATIVE_UFS root: tail anchor manifest=%llx bytes=%llx\n",
            anchor.manifestLba, anchor.imageBytes);
    if (!nxRootRead (static_cast<uint32_t> (anchor.manifestLba), 1, blk)) {
      *why = "adopt anchored manifest read";
      return false;
    }
    SHA256_CTX context;
    uint8_t digest[32];
    SHA256_Init (&context);
    SHA256_Update (&context, blk, 4096);
    SHA256_Final (digest, &context);
    if (memcmp (digest, anchor.manifestSha256, 32)) {
      *why = "adopt anchored manifest checksum";
      return false;
    }
  }
  if (!rootManifestFromBlock (layout, blk, why)) return false;
  printf ("NATIVE_UFS root: manifest status=0 windows=%u objects=%u image=%llx\n",
          layout->windows, layout->objects, layout->imageLba);
  return true;
}
'''


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"patch-native-ufs-root-anchor: anchor not found: {label}")
    return text.replace(old, new, 1)


def patch_layout() -> None:
    text = LAYOUT.read_text(encoding="utf-8", errors="surrogateescape")
    if MARKER in text:
        return
    text = replace_once(
        text,
        "  uint64_t userdataFirst, userdataLast;   // 4096-byte LBAs on LUN 0\n",
        "  uint64_t userdataFirst, userdataLast;   // 4096-byte LBAs on LUN 0\n"
        "  uint64_t imageLba;                      // first block of the root image\n",
        "RootLayout.imageLba",
    )
    start = text.find("static bool findRootLayout (RootLayout *layout, const char **why)\n{")
    if start < 0:
        raise SystemExit("patch-native-ufs-root-anchor: findRootLayout not found")
    text = text[:start] + HELPERS + FIND_ROOT
    LAYOUT.write_text(text, encoding="utf-8", errors="surrogateescape")


def patch_native() -> None:
    text = NATIVE.read_text(encoding="utf-8", errors="surrogateescape")
    if "native-ufs-noverify" in text:
        text = "\n".join(line for line in text.splitlines()
                         if "native-ufs-noverify" not in line) + "\n"
    if MARKER in text and "layout->imageLba" in text:
        NATIVE.write_text(text, encoding="utf-8", errors="surrogateescape")
        return
    start = text.find("static bool nxAdoptRootLayout (RootLayout *layout, const char **why)\n{")
    end_marker = "\n// Boot the root image's windows as block devices."
    end = text.find(end_marker, start)
    if start < 0 or end < 0:
        raise SystemExit("patch-native-ufs-root-anchor: nxAdoptRootLayout not found")
    text = text[:start] + ADOPT_ROOT + text[end:]
    text = text.replace("struct StoreGeometry {\n  bool valid;\n  uint64_t first, last, imageBytes, entriesLba;",
                        "struct StoreGeometry {\n  bool valid;\n  uint64_t first, last, imageLba, imageBytes, entriesLba;", 1)
    text = text.replace("gStoreGeometry = { true, layout->userdataFirst, layout->userdataLast, layout->imageBytes,",
                        "gStoreGeometry = { true, layout->userdataFirst, layout->userdataLast, layout->imageLba, layout->imageBytes,", 1)
    text = text.replace("gRootObjectBase = layout->userdataFirst * 4096ULL;",
                        "gRootObjectBase = layout->imageLba * 4096ULL;", 1)
    root_mode_old = "  if (rootMode && !probe) {\n    return startRoot (provider);\n  }"
    root_mode_new = ("  if (rootMode && !probe) {\n"
                     "    printf (\"NATIVE_UFS: cold init %d\\n\", board_ufs_cold_init ());\n"
                     "    if (!nxAdoptBind ()) return false;\n"
                     "    return startRoot (provider);\n"
                     "  }")
    text = replace_once(text, root_mode_old, root_mode_new, "normal root mode queue")
    text = text.replace("uint64_t absolute = layout->userdataFirst * 4096ULL + layout->window[index].offset;",
                        "uint64_t absolute = layout->imageLba * 4096ULL + layout->window[index].offset;")
    text = text.replace("NATIVE_UFS root windows\\n\"\n"
                        "           \"userdata first=%llx last=%llx manifest status=%u windows=%u\\n\"",
                        "NATIVE_UFS root windows\\n\"\n"
                        "           \"userdata first=%llx last=%llx image=%llx manifest status=%u windows=%u\\n\"", 1)
    text = text.replace("layout->userdataFirst, layout->userdataLast, layout->manifestStatus, layout->windows,",
                        "layout->userdataFirst, layout->userdataLast, layout->imageLba, layout->manifestStatus, layout->windows,", 1)
    store_old = '''    uint64_t partition = gStoreGeometry.last - gStoreGeometry.first + 1;
    uint64_t imageBlocks = (gStoreGeometry.imageBytes + 4095) / 4096;
    const uint64_t reserve = reserveBlocks ();
    if (!blocks || reserve >= partition || blocks > partition - reserve) reason = "size";
    else if (partition - blocks - reserve < imageBlocks + 1024) reason = "overlaps the root image";
    else {
      // The reserve at the end of the partition belongs to the diagnostic log (its last 32 MiB, with the export region below them);
      // the store ends just below it.
      uint64_t first = gStoreGeometry.last + 1 - reserve - blocks;
      if (storeMode () == 2 && !hostStoreOpen (first, blocks)) reason = "window refused";
      else {
        gStoreRegionFirst = first;
        gStoreRegionBlocks = blocks;
        if (firstLba) *firstLba = first;
        ok = 1;
      }
    }'''
    store_new = '''    uint64_t partition = gStoreGeometry.last - gStoreGeometry.first + 1;
    uint64_t imageBlocks = (gStoreGeometry.imageBytes + 4095) / 4096;
    const uint64_t reserve = reserveBlocks ();
    if (!blocks || reserve >= partition || blocks > partition - reserve) reason = "size";
    else {
      uint64_t first = gStoreGeometry.last + 1 - reserve - blocks;
      uint64_t imageEnd = gStoreGeometry.imageLba + imageBlocks;
      if (first < imageEnd && gStoreGeometry.imageLba < first + blocks) reason = "overlaps the root image";
      else if (first >= imageEnd && first - imageEnd < 1024) reason = "root image guard";
      else if (first + blocks <= gStoreGeometry.imageLba && gStoreGeometry.imageLba - (first + blocks) < 1024)
        reason = "root image guard";
      else if (storeMode () == 2 && !hostStoreOpen (first, blocks)) reason = "window refused";
      else {
        gStoreRegionFirst = first;
        gStoreRegionBlocks = blocks;
        if (firstLba) *firstLba = first;
        ok = 1;
      }
    }'''
    text = replace_once(text, store_old, store_new, "persistent store placement")
    log_old = '''    uint64_t partition = gStoreGeometry.last - gStoreGeometry.first + 1;
    uint64_t imageBlocks = (gStoreGeometry.imageBytes + 4095) / 4096;
    if (reserveBlocks () < kLogReserveBlocks + kExportBlocks) reason = "reserve too small for the log";
    else if (partition - reserveBlocks () < imageBlocks + 1024) reason = "overlaps the root image";
    else {
      uint64_t first = gStoreGeometry.last + 1 - kLogReserveBlocks;
      // One write window covers the export region and the log: [first - kExportBlocks, first + kLogReserveBlocks).
      if (!hostLogOpen (first - kExportBlocks, kExportBlocks + kLogReserveBlocks)) reason = "window refused";
      else {
        gLogRegionFirst = first;
        gLogRegionBlocks = kLogReserveBlocks;
        if (firstLba) *firstLba = first;
        if (blocks) *blocks = kLogReserveBlocks;
        ok = 1;
      }
    }'''
    log_new = '''    uint64_t imageBlocks = (gStoreGeometry.imageBytes + 4095) / 4096;
    uint64_t first = gStoreGeometry.last + 1 - kLogReserveBlocks;
    uint64_t imageEnd = gStoreGeometry.imageLba + imageBlocks;
    if (reserveBlocks () < kLogReserveBlocks + kExportBlocks) reason = "reserve too small for the log";
    else if (first - kExportBlocks < imageEnd && gStoreGeometry.imageLba < first + kLogReserveBlocks)
      reason = "overlaps the root image";
    else if (first - kExportBlocks >= imageEnd && first - kExportBlocks - imageEnd < 1024)
      reason = "root image guard";
    else if (!hostLogOpen (first - kExportBlocks, kExportBlocks + kLogReserveBlocks)) reason = "window refused";
    else {
      gLogRegionFirst = first;
      gLogRegionBlocks = kLogReserveBlocks;
      if (firstLba) *firstLba = first;
      if (blocks) *blocks = kLogReserveBlocks;
      ok = 1;
    }'''
    text = replace_once(text, log_old, log_new, "diagnostic log placement")
    NATIVE.write_text(text, encoding="utf-8", errors="surrogateescape")


def main() -> int:
    if not NATIVE.is_file() or not LAYOUT.is_file():
        print(f"patch-native-ufs-root-anchor: ShengUfs sources are missing: "
              f"{NATIVE} / {LAYOUT}", file=sys.stderr)
        return 1
    patch_layout()
    patch_native()
    print("== patch-native-ufs-root-anchor: dynamic tail locator and version-2 objects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
