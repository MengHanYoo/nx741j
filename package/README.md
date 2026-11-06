# Board package (`kits/nx741j`)

The build tree maps this directory to `kits/nx741j`; `board.toml` sets `kit = "nx741j"`.
It carries board-level code that has no shared home:

- EFI drivers and `Kit.toml` match rules used by the UniXNU boot manager on this board,
- board device-tree overlays,
- kext bundles and the OS ABI inputs they need,
- board services that only exist for NX741J.

Nothing here yet. The current bring-up keeps every board driver in the vendor UEFI tree
(`uefi-nx741j`), so this package is intentionally empty; `.gitkeep` keeps it in Git. Fill it when the
first board driver moves out of the firmware tree, and list the ownership paths in `porting.toml` under
the matching area.

Do not put local build inputs, firmware blobs or generated trees here: local inputs go to the Git-ignored
`local.toml` / `unixnu.local.toml`, firmware belongs to `proprietary-files.txt` and `.cache/firmware`.
