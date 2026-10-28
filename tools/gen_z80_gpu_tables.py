#!/usr/bin/env python3
"""Generate the GPU OPP / GMU frequency tables from the phone's own device tree.

    gen_z80_gpu_tables.py FULL.dts [--patch wsl/pandora_gpu.c]

FULL.dts is the live device tree of the target phone, dumped with

    adb exec-out "su -c 'cat /sys/firmware/fdt'" > z80-live.dtb
    dtc -I dtb -O dts -o z80-live.dts z80-live.dtb

It reads, from qcom,kgsl-3d0@3d00000 and qcom,gmu@3d37000:

  qcom,gpu-pwrlevel@N   qcom,gpu-freq (Hz), qcom,level (ARC), qcom,bus-freq
  qcom,gmu-freq-table   pairs of (Hz, vote)

and prints gpu_opps, gmu_opps and gpu_opp_bw in the shapes
wsl/pandora_gpu.c uses, so the table is never copied from another device.
"""
import re
import sys


def block(txt, name):
    i = txt.find(name)
    if i < 0:
        return None
    start = txt.find('{', i)
    depth = 0
    k = start
    while k < len(txt):
        if txt[k] == '{':
            depth += 1
        elif txt[k] == '}':
            depth -= 1
            if depth == 0:
                break
        k += 1
    return txt[start:k + 1]


def val(body, key):
    m = re.search(key + r'\s*=\s*<(0x[0-9a-fA-F]+|\d+)>', body)
    return int(m.group(1), 0) if m else None


def gpu_tables(txt):
    gpu = block(txt, 'qcom,kgsl-3d0@3d00000')
    if not gpu:
        raise SystemExit('qcom,kgsl-3d0@3d00000 not found: is this the target phone\'s DT?')
    levels = []
    for m in re.finditer(r'qcom,gpu-pwrlevel@(\d+)\s*\{(.*?)\};', gpu, re.S):
        b = m.group(2)
        f, arc, bus = val(b, 'qcom,gpu-freq'), val(b, 'qcom,level'), val(b, 'qcom,bus-freq')
        if f and arc:
            levels.append((f, arc, bus))
    levels.sort()                                   # ascending frequency
    gmu = block(txt, 'qcom,gmu@3d37000') or ''
    m = re.search(r'qcom,gmu-freq-table\s*=\s*<([^>]+)>', gmu)
    nums = [int(x, 0) for x in m.group(1).split()] if m else []
    gmu_pairs = [(nums[i], nums[i + 1]) for i in range(0, len(nums) - 1, 2)]
    return levels, gmu_pairs


def emit(levels, gmu_pairs):
    out = ['/* Generated from the phone\'s own device tree by tools/gen_z80_gpu_tables.py. */']
    out.append('static const struct { uint32_t hz, level; } gpu_opps[] = {')
    cur = ['{ %d, 0x%x }' % (f, arc) for f, arc, _ in levels]
    for i in range(0, len(cur), 4):
        out.append('\t' + ', '.join(cur[i:i + 4]) + ',')
    out.append('};')
    out.append('static const struct { uint32_t hz, level; } gmu_opps[] = {')
    cur = ['{ %d, 0x%x }' % (f, v) for f, v in gmu_pairs]
    for i in range(0, len(cur), 4):
        out.append('\t' + ', '.join(cur[i:i + 4]) + ',')
    out.append('};')
    bw = [b if b is not None else 1 for _, _, b in levels]
    out.append('static const uint8_t gpu_opp_bw[] = { ' + ', '.join(str(b) for b in bw) + ' };')
    return '\n'.join(out)


def patch(path, levels, gmu_pairs):
    s = open(path).read()
    m = re.search(r'static const struct \{ uint32_t hz, level; \} gpu_opps\[\] = \{.*?\n\};', s, re.S)
    assert m, 'gpu_opps table not found'
    body = emit(levels, gmu_pairs)
    gpu = re.search(r'static const struct \{ uint32_t hz, level; \} gpu_opps\[\] = \{.*?\n\};', body, re.S).group(0)
    gmu = re.search(r'static const struct \{ uint32_t hz, level; \} gmu_opps\[\] = \{.*?\n\};', body, re.S).group(0)
    s = s[:m.start()] + gpu + '\n' + gmu + s[m.end():]
    mb = re.search(r'static const uint8_t gpu_opp_bw\[\] = \{[^}]*\};', s)
    assert mb, 'gpu_opp_bw not found'
    bw = [b if b is not None else 1 for _, _, b in levels]
    s = s[:mb.start()] + 'static const uint8_t gpu_opp_bw[] = { ' + ', '.join(str(b) for b in bw) + ' };' + s[mb.end():]
    open(path, 'w').write(s)
    print(f'patched {path}')


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    txt = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    levels, gmu_pairs = gpu_tables(txt)
    print(f'# {len(levels)} GPU levels: {levels[0][0] // 1000000}..{levels[-1][0] // 1000000} MHz')
    print(f'# {len(gmu_pairs)} GMU levels: ' + ', '.join(str(f // 1000000) for f, _ in gmu_pairs))
    print(emit(levels, gmu_pairs))
    if '--patch' in sys.argv:
        patch(sys.argv[sys.argv.index('--patch') + 1], levels, gmu_pairs)


if __name__ == '__main__':
    main()
