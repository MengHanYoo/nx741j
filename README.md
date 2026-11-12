# UniXNU-nx741j

**English** | [简体中文](README.zh-CN.md)

Device repository for the **Nubia Z80 Ultra (NX741J)** — Qualcomm **SM8850** (Kaanapali,
codename `canoe`), 8 cores, Adreno 840, UFS, panel `bf375`/`rm692h5` 1440×3200.

The port boots Apple's XNU through the official UniXNU chain:

```
bootloader -> efisp loader -> UniXNU (UEFI) -> DarwinBoot -> XNU -> userland
```

Target: **iOS 16.7.16 (20H392)**, XNU **8796.141.3**, arm64, 4 KiB pages. The OS profile is pinned
in [board.toml](board.toml); it is the only IPSW available for this port.

## Status

| Stage | Scope | State |
|---|---|---|
| 1 | board description, components, build inputs | done |
| 2 | firmware (UniXNU UEFI app, DarwinBoot) | done |
| 3 | payload (kernel, ADT, trust cache) | done |
| 4 | boot chain hand-offs (loader -> UEFI -> DarwinBoot -> XNU) | done |
| 5 | kernel bring-up (platform expert, IOKit, prelinked kexts) | **done** |
| 6 | storage (UFS) | **in progress** |
| 7 | userland (root volume, `launchd`) | blocked by stage 6 |

**Stage 5 milestone (2026-11-12).** The kernel console now renders on the panel, so every boot stage
is observable on the device instead of a colour-bar screen:

```
Registered class IOResources.
...
Starting from prelinked kernel.
kext com.apple.kpi.bsd Loaded.
...
loading dependencies com.apple.kpi.mach
```

**Stage 6 (UFS).** `NativeUfs` matches `/arm-io/ufshc`, takes the controller over and installs its
own request list. Every doorbell write still stalls the bus: the controller only accepts commands in
the state the bootloader left it, and that state does not survive a host-controller reset because this
kernel does not initialise the UFS PHY/clock stack. A vendor-derived cold-init path
(`native-ufs-cold-init=1`: PHY calibration tables, SERDES start, PCS ready, HCE, DME link startup)
is implemented and awaiting device verification.

## Building

Two entry points, both run on Linux/WSL:

```sh
# Fast iteration: kernel -> payload -> firmware, no QEMU smoke.
./scripts/build-fast.sh ~/nx741j-build/UniXNU

# Milestone gate: the same three steps plus a QEMU boot-chain run.
# Use it before flashing anything that touches the firmware memory layout.
sudo ./scripts/build-and-test.sh ~/nx741j-build/UniXNU
```

The gate asserts three markers — `UNIXNU: BootEntry`, `MARK #29`, `QEMU: EBS ok, entering the
kernel` — and fails the build when any is missing.

## Flashing

The vendor ABL on this device rejects `fastboot boot`, `fastboot flash` and `fastboot erase`
(`unknown command`), so everything goes through **9008** and the EFI system partition:

| Region | LUN | LBA | Size |
|---|---|---|---|
| `efisp` (our loader) | 4 | 748894 | 3 MiB |
| `rawdump` — firmware FD | 0 | 4760360 | 4 MiB |
| `rawdump` — payload | 0 | 4743976 | 64 MiB |

```powershell
wsl\flash-boota-9008.ps1 -Img <image> -StartSector <lba> -Label <label> -Lun <lun> -PartitionBytes <bytes>
```

Any Android boot overwrites `efisp`, so it has to be re-flashed before every payload test. Writing
back the stock `efisp` (`sha256 4df18359...`) restores the vendor boot chain.

## Repository layout

| Path | Contents |
|---|---|
| `board.toml` | board description: components, kernel archives, prelink roots, boot args |
| `dts/` | ADT source for the payload |
| `loader/` | efisp EFI loader (`p5.c` ... `p7.c`) and its images |
| `package/firmware-overlay/` | board-level firmware code, applied into the UEFI tree |
| `patches/xnu/` | device XNU patch series (see `series` for the applied order) |
| `patches/firmware/` | firmware patches (DSC/FDF, platform drivers) |
| `scripts/` | build helpers (`build-fast.sh`, `build-and-test.sh`, ...) |
| `docs/` | analysis, audits, per-stage notes, hardware inventory |

## Recent fixes worth knowing

| Patch / change | Why |
|---|---|
| `0010-console-acquire-screen.patch` | `PE_create_console` uses `kPEAcquireScreen`, so `kprintf` reaches the panel |
| `0015-iodevicetree-null-interrupt-parent.patch` | `IODTMapOneInterrupt` fed a NULL parent into `IODTGetICellCounts` and panicked during `InitIOKit` |
| `component:GopFramebuffer` in `[kernel] archives` | the display component was selected but never linked |
| `native-ufs-cold-init=1` | vendor-derived PHY calibration + link startup before any command |

## Documentation

- [docs/TASKS.md](docs/TASKS.md) — consolidated task and status log
- [docs/STATUS-2026-11-11.md](docs/STATUS-2026-11-11.md) — stage-by-stage status
- [docs/BOOT-CHAIN.md](docs/BOOT-CHAIN.md) — boot chain against the official description
- [docs/UFS-QCOM-PHY-PORT.md](docs/UFS-QCOM-PHY-PORT.md) — restored PHY tables and call order
- [docs/HARDWARE-INVENTORY.md](docs/HARDWARE-INVENTORY.md) — hardware facts with sources

## Licence

Patches and board code follow the upstream UniXNU licensing (BSD-2-Clause-Patent where noted in the
individual files). Vendor-derived tables are data recovered from the device's own firmware.
