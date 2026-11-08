# Board package (`kits/nx741j`)

The build tree maps this directory to `kits/nx741j`; `board.toml` sets `kit = "nx741j"`.

## Contents

- `firmware-overlay/` — board-level UEFI code that has no shared home, laid out exactly like the
  `uefi-nx741j` tree (`Platforms/ZTE/nx741jPkg/Drivers/...`). `board.toml [firmware] preflight`
  runs `scripts/apply-firmware-overlay.py`, which copies it into the UEFI tree before the build, so a
  clean checkout reproduces the firmware.
- Board device-tree overlays, kext bundles and `Kit.toml` match rules go here when they are needed.

## Rules

- Do not put local build inputs, firmware blobs or generated trees here: local inputs go to the
  Git-ignored `local.toml` / `unixnu.local.toml`, firmware belongs to `proprietary-files.txt` and
  `.cache/firmware`.
