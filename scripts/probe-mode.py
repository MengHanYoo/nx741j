#!/usr/bin/env python3
"""Toggle the kernel bring-up probe in board.toml (on: reset after SYMBOL, off: none).

    scripts/probe-mode.py on  _PE_create_console _arm_init 1
    scripts/probe-mode.py off
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'board.toml'
KEYS = ('entry_probe_reset_delay', 'entry_probe_reset_after', 'entry_probe_reset_in',
        'entry_probe_reset_occurrence', 'entry_probe_reset_before')


def main(argv):
    if len(argv) < 2 or argv[1] not in ('on', 'off'):
        raise SystemExit(__doc__)
    text = BOARD.read_text()
    lines = [l for l in text.splitlines() if not any(l.strip().startswith(k) for k in KEYS)]
    if argv[1] == 'on':
        if len(argv) < 5:
            raise SystemExit('on needs SYMBOL FUNCTION OCCURRENCE')
        symbol, function, occurrence = argv[2], argv[3], argv[4]
        insert = [
            '# 阶段 5 二分探针：内核入口后 delay 秒复位（复位点在 FUNCTION 调 SYMBOL 之后）。',
            'entry_probe_reset_delay = 3',
            f'entry_probe_reset_after = "{symbol}"',
            f'entry_probe_reset_in = "{function}"',
            f'entry_probe_reset_occurrence = {occurrence}',
        ]
        at = next(i for i, l in enumerate(lines) if l.strip().startswith('panic_screen'))
        lines[at:at] = insert
        text = '\n'.join(lines).replace('panic_screen = true', 'panic_screen = false')
    else:
        text = '\n'.join(lines).replace('panic_screen = false', 'panic_screen = true')
    BOARD.write_text(text + '\n')
    print('probe', argv[1], '->', BOARD)


if __name__ == '__main__':
    main(sys.argv)
