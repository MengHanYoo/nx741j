# Board package (`kits/nx741j`)

The build tree maps this directory to `kits/nx741j`; `board.toml` sets `kit = "nx741j"`.

## Contents

- `firmware-overlay/` — board-level UEFI code that has no shared home, laid out exactly like the
  `uefi-nx741j` tree (`Platforms/ZTE/nx741jPkg/Drivers/...`). Apply it to a checkout with `python3 scripts/apply-firmware-overlay.py <uefi-tree>` before the
  firmware build. It is kept out of the `[firmware] preflight` hook because that hook runs
  tree-relative scripts without arguments, while this installer needs the UEFI tree path.
- Board device-tree overlays, kext bundles and `Kit.toml` match rules go here when they are needed.

## Rules

- Do not put local build inputs, firmware blobs or generated trees here: local inputs go to the
  Git-ignored `local.toml` / `unixnu.local.toml`, firmware belongs to `proprietary-files.txt` and
  `.cache/firmware`.
