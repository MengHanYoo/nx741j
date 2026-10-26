#!/usr/bin/env python3
"""计算 Mach-O 的 cdhash（CodeDirectory 的 SHA256 前 20 字节），并生成 raw 信任缓存模块"""
import struct, hashlib, sys, uuid

def cdhash(path):
    d = open(path, 'rb').read()
    magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags = struct.unpack_from('<IiiIIII', d, 0)
    assert magic == 0xfeedfacf, hex(magic)
    off = 32
    cs_off = cs_size = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', d, off)
        if cmd == 0x1d:  # LC_CODE_SIGNATURE
            cs_off, cs_size = struct.unpack_from('<II', d, off + 8)
        off += cmdsize
    assert cs_off is not None, 'no code signature'
    blob = d[cs_off:cs_off + cs_size]
    # SuperBlob: magic(4) length(4) count(4) then index (type, offset) pairs
    magic, length, count = struct.unpack_from('>III', blob, 0)
    cd = None
    for i in range(count):
        btype, boff = struct.unpack_from('>II', blob, 12 + 8 * i)
        if btype == 0:  # CodeDirectory
            cd = blob[boff:]
            break
    assert cd is not None, 'no CodeDirectory'
    return hashlib.sha256(cd).digest()[:20]

def tc_module(entries, ident='plugin'):
    """version=2, uuid, count, 24-byte entries (cdhash20 + flags 4) sorted"""
    entries = sorted(entries, key=lambda e: e[:20])
    u = hashlib.sha256(ident.encode()).digest()[:16]
    return struct.pack('<I16sI', 2, u, len(entries)) + b''.join(entries)

if __name__ == '__main__':
    exe = sys.argv[1]
    out = sys.argv[2]
    h = cdhash(exe)
    print('cdhash:', h.hex())
    mod = tc_module([h + bytes.fromhex('02000200')])
    open(out, 'wb').write(mod)
    print('module:', out, len(mod), 'bytes')
    print('hex:', mod.hex())
