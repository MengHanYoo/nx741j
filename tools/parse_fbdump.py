#!/usr/bin/env python3
"""Extract the frames of a `pandora_fbdump=SECS` dump from a pulled iOS log file.

    parse_fbdump.py LOG.bin OUTDIR

The dump lives in the log file's dump area: header at a 4 KiB block ("PANDFBDM"),
two 1/3-scale BGRA frames after it (SweetFramebuffer.cpp: pandora_fbdump_head /
pandora_fbdump_scale). Writes <name>.png per frame when Pillow is present, else .ppm.
"""
import os
import struct
import sys

MAGIC = b"PANDFBDM"


def parse(buf):
    idx = buf.find(MAGIC)
    if idx < 0:
        return []
    magic, version, seq, frames, scale = struct.unpack_from("<8sIIII", buf, idx)
    uptime, = struct.unpack_from("<Q", buf, idx + 24)
    f = []
    for i in range(2):
        off = idx + 32 + i * 28
        name, w, h, foff, fmt = struct.unpack_from("<16sIIII", buf, off)
        f.append({"name": name.split(b"\0")[0].decode("ascii", "replace"),
                  "w": w, "h": h, "off": foff, "fmt": fmt})
    return [{"version": version, "seq": seq, "frames": frames, "scale": scale,
             "uptime_ms": uptime, "files": f, "base": idx}]


def save(buf, d, outdir):
    os.makedirs(outdir, exist_ok=True)
    written = []
    for i, meta in enumerate(d["files"]):
        if not meta["w"] or not meta["h"]:
            continue
        start = d["base"] + meta["off"]
        need = meta["w"] * meta["h"] * 4
        if start + need > len(buf):
            need = len(buf) - start
            if need <= 0:
                continue
        data = buf[start:start + need]
        w, h = meta["w"], meta["h"]
        name = f"{meta['name'] or 'frame%d' % i}_{w}x{h}_seq{d['seq']}"
        path = os.path.join(outdir, name + (".png" if _have_pil() else ".ppm"))
        if _have_pil():
            from PIL import Image
            img = Image.frombytes("RGBA", (w, h), data[:w * h * 4])
            img = img.convert("RGB")
            img.save(path)
        else:
            with open(path, "wb") as o:
                o.write(b"P6\n%d %d\n255\n" % (w, h))
                out = bytearray()
                for k in range(0, w * h * 4, 4):
                    b, g, r = data[k], data[k + 1], data[k + 2]
                    out += bytes((r, g, b))
                o.write(out)
        written.append(path)
    return written


def _have_pil():
    try:
        import PIL  # noqa: F401
        return True
    except Exception:
        return False


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    buf = open(sys.argv[1], "rb").read()
    dumps = parse(buf)
    if not dumps:
        print("没有找到 PANDFBDM（这次开机没开 pandora_fbdump，或还没到 20 秒）")
        return
    for d in dumps:
        print(f"seq {d['seq']} frames {d['frames']} scale {d['scale']} uptime {d['uptime_ms']} ms")
        for p in save(buf, d, sys.argv[2]):
            print("  wrote", p)


if __name__ == "__main__":
    main()
