#!/usr/bin/env python3
"""Insert the board UFS cold-init hook into the ShengUfs driver.

The driver is an upstream component (UniXNU/ShengUfs at a pinned revision), so the
board cannot edit it in place. This step runs from [kernel] archives *before*
component:ShengUfs compiles NativeUfs.cpp and makes exactly two insertions:

  * declare board_ufs_cold_init(), implemented in package/storage-bringup/ufs-cold-init.c
  * call it once from NativeUfs::start before any controller access

The call is a no-op unless native-ufs-cold-init=1, which keeps the firmware
takeover path as the default. Re-running the step is harmless.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/ShengUfs/NativeUfs.cpp'
DECLARATION = 'extern "C" int board_ufs_cold_init (void);'
ANCHOR_DECL = 'void boot_progress_poll (void) __attribute__((weak));'
ANCHOR_CALL = '  native_ufs_probe_requested = probe != 0;'
CALL = '  if (probe || rootMode) {\n    board_ufs_cold_init ();\n  }'

if not SOURCE.is_file():
    raise SystemExit(f'patch-native-ufs-cold-init: missing {SOURCE}')

text = SOURCE.read_text()
if 'board_ufs_cold_init' in text:
    print('== patch-native-ufs-cold-init: already applied')
    raise SystemExit(0)

if ANCHOR_DECL not in text or ANCHOR_CALL not in text:
    raise SystemExit('patch-native-ufs-cold-init: anchors not found; driver changed')

text = text.replace(ANCHOR_DECL, ANCHOR_DECL + '\n' + DECLARATION, 1)
text = text.replace(ANCHOR_CALL, CALL + '\n' + ANCHOR_CALL, 1)
SOURCE.write_text(text)
print('== patch-native-ufs-cold-init: applied')
