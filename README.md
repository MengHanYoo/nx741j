# UniXNU-nx741j

Device repository for the **Nubia Z80 Ultra (NX741J)** — Qualcomm **SM8850** (Kaanapali, codename
`canoe`), 8 cores (6 + 2), Adreno 840, UFS KLUGG8NHKB, panel `bf375`/`rm692h5` 1440×3200.

Target: **iOS 16.7.16 (20H392)**, XNU **8796.141.3**, arm64, 4 KiB pages. The OS profile is pinned in
[board.toml](board.toml) (`os_profile = "ios-16.7.16-20H392"`); it is the only IPSW available for this
port, so the iPadOS 17 / 10063 line is not a valid substitute.

## 本地开发

本机路径与 SoC 本地检出写在 Git 忽略的 `local.toml` 中，步骤与验证命令见 [docs/LOCAL-DEVELOPMENT.md](docs/LOCAL-DEVELOPMENT.md)。

## 当前进度 / Current status

**2026-11-10 更新**：内核已越过 `PE_create_console` 与平台专家，进入 `KLDBootstrap` 的 **kext 加载**；
阶段 6（存储 / 根卷）尚未开始。

**2026-11-10 update**: the kernel now gets past `PE_create_console` and the platform expert and is
loading kexts in `KLDBootstrap`; stage 6 (storage / root volume) has not started.

| 层 / Layer | 状态 / State |
|---|---|
| 1 启动镜像 / boot image | ✅ efisp loader 从 `rawdump` 读 payload 与 FD，校验后把 FD 拷到 `0xFF300000`，`x0` 带 DT 进 SEC / efisp loader reads payload + FD from `rawdump`, verifies both, copies the FD to `0xFF300000` and enters SEC with the DT in `x0` |
| 2 UEFI 固件 / firmware | ✅ 4 MiB FD 起得来：DXE/BDS、GOP、输入、RAM 盘 + FAT、`AcpiTableDxe` 都在；厂商 UFS 栈未启用（见下）/ the 4 MiB FD reaches BDS with GOP, input, RAM-disk + FAT and `AcpiTableDxe`; the vendor UFS stack stays disabled (see below) |
| 3 UniXNU 启动管理器 / boot manager | ✅ 菜单出现、`unixnu.toml` 解析、kit/驱动/ACPI 注入可用 / the menu comes up, `unixnu.toml` parses, kit/driver/ACPI injection works |
| 4 DarwinBoot | ✅ QEMU 闸门三标记全绿（`BootEntry` / `MARK #29` / `EBS ok`），真机已交接进内核 / the QEMU gate's three markers pass and the device reaches the kernel hand-off |
| 5 内核 / kernel | 🟡 过 `PE_create_console`、`PE_init_platform(TRUE)`、`machine_startup`，停在 kext 加载 / past `PE_create_console`, `PE_init_platform(TRUE)` and `machine_startup`, now in kext loading |
| 6 用户态 / user land | ⬜ 未开始 / not started |

### 关键修复 / The fix that unblocked it

阶段 5 卡了整整一夜的“视频控制台崩溃”是假象：`pexpert/arm64/BCM2837.h` 把
`PLATFORM_PANIC_LOG_PADDR` 硬编码成树莓派地址 `0x3c0fc000`，`check_for_panic_log()` 在建控制台前
就映射并读取它。补丁 `patches/xnu/0002-bcm2837-drop-panic-log-paddr.patch` 去掉该宏后，
`PE_create_console` → `PE_init_platform(TRUE)` → `machine_startup` 依次通过（各用 3 秒 PSCI 复位探针确认）。

The stage-5 “video console crash” that cost a night was a red herring: `pexpert/arm64/BCM2837.h`
hardcodes `PLATFORM_PANIC_LOG_PADDR` to the Raspberry Pi address `0x3c0fc000`, and
`check_for_panic_log()` maps and reads it before the console exists. With
`patches/xnu/0002-bcm2837-drop-panic-log-paddr.patch` the macro is gone and `PE_create_console`,
`PE_init_platform(TRUE)` and `machine_startup` all pass, each confirmed by a 3 second PSCI reset probe.

### 正在打 / In flight

- kext 加载期的崩溃：类重复注册、`ArmPlatform` 顺序、未实现 prototype、常量写保护，以及
  `IOService::waitForService` 的无界等待（看门狗补丁让缺失 provider 直接 panic 报名字）。
  / kext-stage crashes: duplicate class registration, `ArmPlatform` ordering, unimplemented prototypes,
  constant write protection, and the unbounded `IOService::waitForService` wait (a watchdog patch makes a
  missing provider panic by name instead of hanging).
- 存储：`ShengUfs` 已链接、`ArmSmmu4K` 已绑进 `libShengUfs.a`、VerifiedRoot 策略本地生成，
  正在核对 ADT 的 `ufshc` 与 SM8850 寄存器差异。
  / storage: `ShengUfs` links, `ArmSmmu4K` is bound into `libShengUfs.a`, the VerifiedRoot policy is generated
  locally, and the ADT `ufshc` node is being checked against the SM8850 registers.
- ADT 现在由实机 DTB 派生（`scripts/mk-adt-dts.py`，5,332 节点），带真实 `clocks`/`iommus`/`dma-coherent`。
  / the ADT is now derived from the device's own DTB (`scripts/mk-adt-dts.py`, 5,332 nodes) and carries real
  `clocks`, `iommus` and `dma-coherent`.

厂商 UFS 栈（`UFSDxe` + `HALIOMMU`）在固件里保持关闭：它依赖本机不存在的 `DTBExtn`/`NPA`/`HWIO` 提供者，
官方路径是内核侧的 `ShengUfs`（[UFS 移植](https://unixnu.com/zh/porting/ufs)）。
/ The vendor UFS stack (`UFSDxe` + `HALIOMMU`) stays off in firmware: it needs `DTBExtn`/`NPA`/`HWIO`
providers this device does not ship, and the documented path is `ShengUfs` inside XNU
([UFS porting](https://unixnu.com/zh/porting/ufs)).

Per-area evidence, findings and remaining inputs live in [porting.toml](porting.toml); the long-form
record is [docs/PORTING-RECORD.md](docs/PORTING-RECORD.md), the current open list is
[docs/OPEN-ITEMS.md](docs/OPEN-ITEMS.md) and the stage-5 log is [docs/STEP5-BRINGUP.md](docs/STEP5-BRINGUP.md).

## Local build (no GitHub Actions)

```sh
scripts/ci-local.sh            # defconfig → fetch → status → tree → record check
                                # → kernel → payload → bootimg → package → firmware
scripts/ci-local.sh --check    # resolve + assemble + record check only
```

The pipeline uses the local WSL UniXNU checkout (`UNIXNU=/path/to/UniXNU`, default `../UniXNU`).
The repository deliberately has no `.github` workflows.

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
| `dts/nx741j.dts` | hand-written board tree (firmware `FdtBlob`, `soc:dts` include path) |
| `dts/nx741j-adt.dts` | ADT source derived from the device's own 5,332-node DTB, generated by `scripts/mk-adt-dts.py` |
| `patches/xnu/series` | board-level XNU patches, in application order |
| `package/` | board packaging; becomes `kits/nx741j` in the build tree |
| `proprietary-files.txt` | firmware file sources and hashes |
| `scripts/package.sh` | the packaging entry point (see below) |
| `scripts/mk-adt-dts.py` | regenerates `dts/nx741j-adt.dts` from `stock/dt/live_fdt.dtb` |
| `unixnu.lock` | resolved source commits; `sm8850` is pinned here |

`package/firmware-overlay/` carries the board's firmware delta (DSC/FDF/APRIORI/DXE.inc, `MemoryMapLib`,
`SmemStubDxe`, `KernelStubDxe`, `AndroidDxe`, …) and is applied to the vendor UEFI tree by
`scripts/apply-firmware-overlay.py`; `package/upstream/` holds the files that shadow the shared UniXNU
tree (DarwinBoot, UniXNULib, the ACPI and RAM-disk drivers).

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
