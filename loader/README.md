# efisp loader (phase 5)

Self-contained EFI application (PE32+, ARM64, subsystem 10, with `.reloc`) that boots the
Mu-Silicium firmware volume from the `efisp` partition, so `boot.img` is no longer involved.

Flow: `BlockIo` → GPT → `rawdump` (payload at +0, firmware volume at +64 MiB) → FDT →
copy the FV to `0xFF300000` → mask interrupts, switch stack, disable MMU/caches →
branch to `0xFF300000` with `x0 = FDT`.

| File | Purpose |
|---|---|
| `p5.c` | working loader (see flow above) |
| `p3n.c` | no-jump variant used to verify the copy (`copy diff words 0x0`) |
| `p3.c`, `p4.c` | earlier probes (memory map, MMU-off jump) |
| `p2.c`, `p2c.c`, `probe.c`, `probe2.c` | device/GT probing and execution probes |
| `build-*.sh` | `clang --target=aarch64-unknown-windows-msvc` + `lld-link /subsystem:efi_application`（`build-p6.sh` 为当前构建） |
| `images/efisp-p5.img` | 3 MiB efisp image (sha256 `1e8bdf04…`), write to LBA 748894 |

Write order (9008): payload (64 MiB) → `rawdump`+0, FD (4 MiB) → `rawdump`+16,384 sectors,
`images/efisp-p5.img` → `efisp` (LBA 748894). Rollback: write the stock `efisp.img`
(sha256 `4df18359…`) back.
