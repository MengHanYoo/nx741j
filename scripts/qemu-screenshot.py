#!/usr/bin/env python3
"""Take one screenshot from a running QEMU over QMP (no display needed).

QEMU writes the image itself: name the output .png and it is a PNG (QEMU >= 7.1
understands format=png), anything else keeps the raw PPM.  Neither needs Pillow,
which is not installed in the WSL image.
"""
import json
import socket
import sys
import time


def qmp(path, commands):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    f = s.makefile('rwb')
    f.readline()
    f.write(b'{"execute":"qmp_capabilities"}\r\n'); f.flush(); f.readline()
    for cmd in commands:
        f.write((json.dumps(cmd) + '\r\n').encode()); f.flush()
        reply = json.loads(f.readline() or b'{}')
        if 'error' in reply:
            raise SystemExit(f'QMP {cmd["execute"]}: {reply["error"]}')
    s.close()


if __name__ == '__main__':
    sock, out, delay = sys.argv[1], sys.argv[2], float(sys.argv[3])
    time.sleep(delay)
    arguments = {'filename': out}
    if out.lower().endswith('.png'):
        arguments['format'] = 'png'
    qmp(sock, [{'execute': 'screendump', 'arguments': arguments}])
    print(f'screenshot: {out}')
