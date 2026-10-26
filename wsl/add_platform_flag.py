#!/usr/bin/env python3
"""给 Mach-O 的 CodeDirectory 加上 CS_PLATFORM_BINARY，重算 cdhash，并生成信任模块"""
import struct, hashlib, sys, shutil

CS_PLATFORM_BINARY = 0x04000000

def parse(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('<I', d, 16)[0]
    off = 32
    cs = None
    for _ in range(n):
        cmd, size = struct.unpack_from('<II', d, off)
        if cmd == 0x1d:
            cs = (off, struct.unpack_from('<II', d, off + 8))
            break
        off += size
    assert cs, 'no signature'
    return d, cs

def cd_offset(d, cs_abs):
    blob_off, blob_len = cs_abs
    magic, length, count = struct.unpack_from('>III', d, blob_off)
    for i in range(count):
        t, o = struct.unpack_from('>II', d, blob_off + 12 + 8 * i)
        if t == 0:
            return blob_off + o
    raise SystemExit('no CD')

def main(src, dst, tcout):
    shutil.copyfile(src, dst)
    d, cs = parse(dst)
    cd = cd_offset(d, cs[1])
    flags = struct.unpack_from('>I', d, cd + 12)[0]
    newflags = flags | CS_PLATFORM_BINARY
    print('CD flags: %#x -> %#x' % (flags, newflags))
    buf = bytearray(d)
    struct.pack_into('>I', buf, cd + 12, newflags)
    open(dst, 'wb').write(bytes(buf))
    d = bytes(buf)
    cd = cd_offset(d, cs[1])
    cdsize = struct.unpack_from('>I', d, cd + 4)[0]
    h = hashlib.sha256(d[cd:cd + cdsize]).digest()[:20]
    print('cdhash:', h.hex())
    mod = struct.pack('<I16sI', 2, hashlib.sha256(b'plugin').digest()[:16], 1) + h + bytes.fromhex('02000200')
    open(tcout, 'wb').write(mod)
    print('trust module:', tcout, len(mod))

main(sys.argv[1], sys.argv[2], sys.argv[3])
