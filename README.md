# UniXNU-nx741j

Device repository for the **Nubia Z80 Ultra (NX741J)** — Qualcomm **SM8850** (Kaanapali, codename
`canoe`), 8 cores (6 + 2), Adreno 840, UFS KLUGG8NHKB, panel `bf375`/`rm692h5` 1440×3200.

Target: **iOS 16.7.16 (20H392)**, XNU **8796.141.3**, arm64, 4 KiB pages. The OS profile is pinned in
[board.toml](board.toml) (`os_profile = "ios-16.7.16-20H392"`); it is the only IPSW available for this
port, so the iPadOS 17 / 10063 line is not a valid substitute.

## Status

| Layer | State |
|---|---|
| 1 boot image / loader | ✅ efisp loader (phase 5) reads the payload and FD from `rawdump`, copies the FD to `0xFF300000` and enters SEC with the DT in `x0` |
| 2 UEFI firmware | ✅ minimal 32-driver FV reaches BDS; `SEC: TLMM MMIO HOB` published; firmware log clean of the former HALIOMMU/DTBExtn/SCM asserts |
| 3 UniXNU boot manager | ✅ menu shows the `NX741J` payload entry; the payload volume is published from the x0 DT (`initrd block device … Success`, `Installed Fat filesystem`) |
| 4 DarwinBoot | ❌ the payload entry ends in a black screen and `ZTE MemoryDump Mode` before any kernel output; `panic_screen` produced no visible panic |
| 5 kernel | XNU 8796.141.3 links from this repository's series; prelink `runtime_qualified=false`, payload/boot-image regeneration reproducible |
| 6 user land | not started |

Known open items: the UEFI keypad (vendor PMIC stack needs RPMH/SCM, the TLMM window read path is under test)
and the stage-4 handoff crash. Details: `docs/STAGE2-CONFORMANCE.md`, `docs/ANALYSIS-EFISP-VENDORBOOT.md`,
`docs/HARDWARE-INVENTORY.md`.

Per-area evidence, findings and remaining inputs live in [porting.toml](porting.toml); the long-form
record is [docs/PORTING-RECORD.md](docs/PORTING-RECORD.md).

## Prerequisites

- Linux (WSL2) root shell with 32 parallel jobs available.
- UniXNU toolchain at `/opt/unixnu-linux` (`scripts/linux/setup-toolchain.py`), XNU sources served from
  the local mirror cache — see [AGENTS.md](AGENTS.md).
- The NX741J IPSW `iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw` (kernel cache, trust cache).
- Vendor UEFI tree (`uefi-nx741j`) for the firmware step. Its location is a local input, never a
  repository path.

Local build inputs (paths, `jobs`, kernel command line) belong in a Git-ignored `local.toml` copied from
[local.toml.example](local.toml.example); they never enter `board.toml`.

## Build

```sh
./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j
./unixnu fetch -D ../UniXNU-nx741j
./unixnu status --json -D ../UniXNU-nx741j      # components, sources, patch order
./unixnu tree -D ../UniXNU-nx741j
```

Kernel and image steps, from the UniXNU checkout:

```sh
./unixnu build kernel -D ../UniXNU-nx741j
./unixnu prelink      -D ../UniXNU-nx741j       # requires [kernel.prelink] in board.toml
./unixnu payload      -D ../UniXNU-nx741j
./unixnu bootimg      -D ../UniXNU-nx741j
```

One command for a clean checkout, per the [packaging guide](https://unixnu.com/zh/porting/packaging):

```sh
scripts/package.sh --check --os ios-16.7.16-20H392   # deps, preflight, patch order, then stop
scripts/package.sh --no-deps                         # build + pack + verify
```

Artifacts land in `out/nx741j/ios-16.7.16-20H392/nx741j/`: `kernel/kernel.macho`,
`payload/payload.img`, `boot/boot.img`, plus `package.manifest.json`.

## Layout

| Path | Contents |
|---|---|
| `board.toml` | SoC/OS selection, components, kernel fragments and patch series, firmware, payload, boot image, packaging inputs |
| `porting.toml` | maintainer and agent record: hardware inventory, firmware versions, component plan, per-layer status |
| `configs/nx741j_defconfig` | `CONFIG_XNU_*` values (`y`/`n` to `XNU_*` for the kernel build) |
| `dts/board.dts` | board device tree (`soc:dts` include path) |
| `patches/xnu/series` | board-level XNU patches, in application order |
| `package/` | board packaging; becomes `kits/nx741j` in the build tree |
| `proprietary-files.txt` | firmware file sources and hashes |
| `scripts/package.sh` | the packaging entry point (see below) |
| `.github/workflows/` | `porting-record.yml` (record check) and `package.yml` (artifact run) |
| `unixnu.lock` | resolved source commits; `sm8850` is pinned here |

`package/` is intentionally empty at this stage: no board-level EFI driver or kext is owned by this
repository yet. Board code currently lives in the vendor UEFI tree (`uefi-nx741j`). When the first
board package lands, it goes here and only here.

## Documentation

- [Port a device](https://unixnu.com/zh/porting/steps) — the eight-step flow this repository follows
- [Device repository](https://unixnu.com/zh/porting/device-repository) — field reference and completion list
- [Packaging](https://unixnu.com/zh/porting/packaging) — `scripts/package.sh` contract
- [Bring-up order](https://unixnu.com/zh/porting/bring-up) — firmware, menu, loader, kernel, root volume
- [Porting record](https://unixnu.com/zh/porting/records) — record format and evidence levels

`scripts/package.sh` here follows the documented six steps (`deps`, `preflight`, `patches`, `build`,
`pack`, `verify`) but is a local implementation: this UniXNU snapshot predates `unixnu release` and
`templates/package.sh`, so the script drives the equivalent `unixnu` subcommands directly. Replace it
verbatim once the upstream template is available.
