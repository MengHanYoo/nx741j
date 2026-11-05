#!/bin/bash
# Package the nx741j boot image: gzip(BootShim.bin + SILICIUM_UEFI.fd), Android header v4,
# no DTB and no ramdisk (see NX741J-HANDOFF-2026-11-05.md section 3).
set -eu
T=/opt/work/uefi-sheng-main
FV="$T/Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd"
SHIM="$T/BootShim/BootShim.bin"
NAME=${1:-boot-uefi-smemstub.img}
OUTDIR=/mnt/e/default-workspace/z80u-collect
WORK=/tmp/pack

rm -rf "$WORK"; mkdir -p "$WORK"

[ -f "$FV" ] || { echo "missing FV: $FV"; exit 1; }
[ -f "$SHIM" ] || { echo "missing shim: $SHIM"; exit 1; }

cat "$SHIM" "$FV" > "$WORK/payload.bin"
python3 - <<'PY'
import gzip, pathlib
p = pathlib.Path('/tmp/pack/payload.bin')
k = pathlib.Path('/tmp/pack/k.gz')
raw = p.read_bytes()
k.write_bytes(gzip.compress(raw, mtime=0))
print(f'payload={len(raw)} gz={k.stat().st_size} armd_off0x38={raw[0x38:0x3c].decode("latin1")!r}')
PY

python3 "$T/Resources/Scripts/mkbootimg.py" \
  --kernel "$WORK/k.gz" --header_version 4 --pagesize 4096 \
  -o "$OUTDIR/$NAME"

ls -l "$OUTDIR/$NAME"
python3 - "$OUTDIR/$NAME" <<'PY'
import struct, sys, gzip
p = sys.argv[1]
b = open(p, 'rb').read()
magic = b[:8]
ksize, kaddr, rsize = struct.unpack_from('<III', b, 8)
psize, = struct.unpack_from('<I', b, 0x24)
print(f'magic={magic!r} kernel_size={ksize} ramdisk_size={rsize} page_size={psize} file={len(b)}')
ker = b[psize:psize+ksize]
d = gzip.decompress(ker)
print(f'kernel decompressed={len(d)} shim_size={len(d)-4194304} fv_hdr={d[0x38+0x10:0x38+0x14]!r}')
print('has _FVH:', b'_FVH' in d, 'tail after gzip zero:', b[psize+ksize:psize+ksize+16].hex())
PY
