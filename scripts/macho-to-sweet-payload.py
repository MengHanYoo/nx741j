#!/usr/bin/env python3
"""Convert a UniXNU kernel.macho into the SWEETXNU payload container that
data-a-live's shim/loader.c understands.

Container layout (little endian, packed; shim/loader/loader.c):

    char     magic[8]        "SWEETXNU"
    uint32   version         must be 1
    uint32   nsegs
    uint64   virt_base       XNU virtBase (kernel __TEXT vmaddr)
    uint64   entry_va        kernel entry virtual address
    uint64   adt_off         offset of the ADT bytes inside this file (8-aligned)
    uint64   adt_size
    uint64   kernel_end      first VA above the kernel's last segment
    uint64   phys_base       physical address of virt_base
    uint64   mem_size        size of the kernel physical region
    char     cmdline[608]
    struct seg { u64 vmaddr, vmsize, file_off, file_size } [nsegs]

The loader copies segment i to phys_base + (vmaddr - virt_base), zero-fills the
rest of vmsize, places the ADT at kernel_end (physical) and builds boot_args
there.  It rejects payloads whose segments are outside its RAM window, so the
window must be given explicitly for this board.

Usage:
  macho-to-sweet-payload.py --kernel kernel.macho --adt hardware.dtb \
      --phys-base 0x... --mem-size 0x... [--cmdline "..."] -o payload.bin
"""
import argparse
import struct

BOOT_LINE_LENGTH = 608
MAGIC = b"SWEETXNU"


def parse_macho(path):
    data = open(path, "rb").read()
    magic, cputype, cpusub, ftype, ncmds, sizeofcmds, flags, _ = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != 0xFEEDFACF:
        raise SystemExit("not a 64-bit Mach-O")
    off = 32
    segs = []
    entry = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            name = data[off + 8:off + 24].rstrip(b"\0").decode("latin1")
            vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, sflags = struct.unpack_from("<QQQQiiII", data, off + 24)
            if filesize or vmsize:
                segs.append(dict(name=name, vmaddr=vmaddr, vmsize=vmsize,
                                 file_off=fileoff, file_size=filesize, prot=initprot))
        elif cmd == 0x5 and cmdsize == 288:  # LC_UNIXTHREAD (arm64, as DarwinBoot parses it)
            if struct.unpack_from("<I", data, off + 8)[0] == 6:
                entry = struct.unpack_from("<Q", data, off + 272)[0]
        off += cmdsize
    if not segs:
        raise SystemExit("no segments")
    if entry is None:
        raise SystemExit("no LC_UNIXTHREAD entry")
    return data, segs, entry


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--adt", required=True)
    ap.add_argument("--phys-base", required=True, type=lambda s: int(s, 0))
    ap.add_argument("--mem-size", required=True, type=lambda s: int(s, 0))
    ap.add_argument("--virt-base", type=lambda s: int(s, 0), default=0xFFFFFFF007004000)
    ap.add_argument("--cmdline", default="keepsyms=1 kextlog=0x1")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    data, segs, entry = parse_macho(args.kernel)
    adt = open(args.adt, "rb").read()

    kernel_end = max(s["vmaddr"] + s["vmsize"] for s in segs)
    phys_low = min(s["vmaddr"] for s in segs) - args.virt_base + args.phys_base
    phys_high = kernel_end - args.virt_base + args.phys_base
    span = phys_high - phys_low
    print("segments      %d" % len(segs))
    print("virt_base     0x%016x" % args.virt_base)
    print("entry_va      0x%016x" % entry)
    print("kernel_end    0x%016x" % kernel_end)
    print("phys span     0x%016x .. 0x%016x (%d bytes)" % (phys_low, phys_high, span))
    if args.mem_size < span:
        raise SystemExit("mem-size 0x%x smaller than the segment span 0x%x" % (args.mem_size, span))

    # body: segments first (8-aligned), then ADT (8-aligned)
    header_len = 8 + 4 + 4 + 5 * 8 + 2 * 8 + BOOT_LINE_LENGTH
    seg_table = header_len + 32 * len(segs)
    body = bytearray()
    base_off = seg_table
    placed = []
    for s in segs:
        off = base_off + len(body)
        body += data[s["file_off"]:s["file_off"] + s["file_size"]]
        while len(body) % 8:
            body += b"\0"
        placed.append((s, off))
    while len(body) % 8:
        body += b"\0"
    adt_off = base_off + len(body)
    body += adt

    out = bytearray()
    out += MAGIC
    out += struct.pack("<II", 1, len(segs))
    out += struct.pack("<QQQQQQQ", args.virt_base, entry, adt_off, len(adt),
                        kernel_end, args.phys_base, args.mem_size)
    cmd = args.cmdline.encode()[:BOOT_LINE_LENGTH - 1]
    out += cmd + b"\0" * (BOOT_LINE_LENGTH - len(cmd))
    for s, off in placed:
        out += struct.pack("<QQQQ", s["vmaddr"], s["vmsize"], off, s["file_size"])
    out += body
    open(args.output, "wb").write(out)
    print("wrote %s: %d bytes (header %d, segs %d, adt %d @0x%x)" %
          (args.output, len(out), header_len, len(segs), len(adt), adt_off))
    assert len(out) == seg_table + len(body)


if __name__ == "__main__":
    main()
