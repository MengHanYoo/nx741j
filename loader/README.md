# efisp loader (phase 7)

Self-contained EFI application (PE32+, ARM64, subsystem 10, with `.reloc`) that boots the
Mu-Silicium firmware volume from the `efisp` partition, so `boot.img` is no longer involved.

Flow: `BlockIo` → GPT → `rawdump` (payload at +0, firmware volume at +64 MiB) → FDT →
copy the FV to `0xFF300000` → mask interrupts, switch stack, disable MMU/caches →
branch to `0xFF300000` with `x0 = FDT`.

| File | Purpose |
|---|---|
| `p7.c` | current loader: boot flow above plus `NXLOG` capture, boot progress, UFS snapshot and SMMU GCD records |
| `p5.c` | earlier working loader without the recovery records |
| `p3n.c` | no-jump variant used to verify the copy (`copy diff words 0x0`) |
| `p3.c`, `p4.c` | earlier probes (memory map, MMU-off jump) |
| `p2.c`, `p2c.c`, `probe.c`, `probe2.c` | device/GT probing and execution probes |
| `build-*.sh` | `clang --target=aarch64-unknown-windows-msvc` + `lld-link /subsystem:efi_application`；`build-p7.sh` 为当前构建 |
| `images/efisp-p7.img` | 3 MiB efisp image, SHA-256 `73b56bf2b2192706ec724974f832457d5b95ab1382038e3442bc392127b4b246`, write to LBA 748894 |

Write order (9008): payload (64 MiB) → `rawdump`+0, FD (4 MiB) → `rawdump`+16,384 sectors,
`images/efisp-p7.img` → `efisp` (LBA 748894). Rollback: write the stock `efisp.img`
(sha256 `4df18359…`) back.

Phase 7 records:

| Record | LBA |
|---|---|
| boot progress | `rawdump + 79 MiB` (`4764200`) |
| NXLOG status | `4764464` |
| NXLOG body | `4764465` onward, up to 127 KiB |
| UFS host snapshot | `4764488` |

The older phase-7 build did not initialize `gRawBase`, so its progress write
landed at absolute LBA `20224` inside `super`. The current source binds
`gRawBase`/`gRawBs` after finding rawdump, refuses a zero base and calls
`dump_ufs_state()`. Use
`scripts/host/repair-super-progress-sector.ps1` to restore only the affected
2026-11-09 backup sector after validating the current BP_RECORD.
