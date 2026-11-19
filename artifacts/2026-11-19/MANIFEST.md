# Artefacts - 2026-11-19

Built and flashed during the storage bring-up session. Every file below was written to the
unit and verified by read-back; the checksums are in `SHA256SUMS`.

| File | Size | Purpose | Written to |
|---|---|---|---|
| `efisp-loader.img` | 3,145,728 B | EFI loader that replaces the vendor `efisp` and starts the UniXNU firmware from `rawdump` | LUN 4, sector 748894, 3 MiB |
| `unixnu-firmware.fd` | 4,194,304 B | Mu firmware volume (DXE, UniXNU menu, DarwinBoot) | LUN 0, sector 4760360 (rawdump + 64 MiB) |
| `payload-probe.img` | 67,108,864 B | payload ramdisk with the `native-ufs-probe=12` kernel used for the root-publication run | LUN 0, sector 4743976, 64 MiB |

The userdata image (`userdata-root.img`, 7.8 GiB, `SHNGROOT` v1) is not stored here because of
its size. Rebuild it with `./unixnu userdata -D <device repo>` and flash it to LUN 0, sector
4,875,048; the manifest read-back is byte-identical and the window layout is recorded in the
device README.

Host-side flashing and log reading live in `scripts/host/`:

- `flash-boota-9008.ps1` writes an image to any LUN/sector and can read back with `-ReadTo`;
- `read-9008.ps1` is the older read-only helper;

Build entry points used for these artefacts are in `scripts/build/` (kernel, payload, QEMU
gate, firmware, and the tree patch step).