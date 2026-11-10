# 未完成项（按官方页面归类）

更新于 2026-11-10 夜（设备仓库 revision `98b9e00`）。状态：`完成` / `进行中` / `阻塞`（缺硬件或信息）/ `不发布`（owner 决策）。

## 本次（11-09 夜 → 11-10）推进

- **阶段 5 越过控制台**：真因是 `BCM2837.h` 的 `PLATFORM_PANIC_LOG_PADDR`（树莓派地址 `0x3c0fc000`），
  `check_for_panic_log()` 在建控制台前映射它 → 每次启动都 MemoryDump。补丁 `0002` 去掉该宏后
  `PE_create_console` / `PE_init_platform(TRUE)` / `machine_startup` 全部通过（各用 3 秒 PSCI 复位探针确认）。
  详见 [STEP5-BRINGUP.md](STEP5-BRINGUP.md)。
- **进入 kext 加载阶段**：当前在打类重复注册、`ArmPlatform` 顺序、未实现 prototype、常量写保护，
  以及 `IOService::waitForService` 的无界等待（加了看门狗，缺失 provider 会 panic 报名字）。
- **存储准备**：`ShengUfs` 已链接、`ArmSmmu4K` 已绑进 `libShengUfs.a`、VerifiedRoot 策略本地生成；
  正在核对 ADT 里的 `ufshc` 与 SM8850 寄存器差异。
- **ADT 改由实机 DTB 派生**：`scripts/mk-adt-dts.py` 从 911 KB / 5,332 节点实机树生成 `dts/nx741j-adt.dts`，
  ADT 现在带真实 `clocks`/`iommus`/`dma-coherent` 与两段 GIC reg。

## 待办总表

| # | 项 | 官方页 | 状态 | 下一步 |
|---|---|---|---|---|
| 1 | 固件集成可复现 | [UEFI 固件](/zh/porting/uefi-firmware) | ✅ 完成 | `package/firmware-overlay/` 已收录 DSC/FDF/incs/驱动；构建前运行 `scripts/apply-firmware-overlay.py <uefi-tree>` |
| 2 | 菜单 → DarwinBoot 交接 | [启动链](/zh/concepts/boot-chain)、[Payload](/zh/concepts/payload) | ✅ 完成 | QEMU 闸门三标记（`BootEntry` / `MARK #29` / `EBS ok`）全绿；真机已进入内核 |
| 3 | 内核启动 → 平台专家 | [启动链](/zh/concepts/boot-chain) | 进行中 | 已过 `PE_create_console`/`PE_init_platform`/`machine_startup`，停在 **kext 加载**；下一步见 [STEP5-BRINGUP.md](STEP5-BRINGUP.md) |
| 4 | 存储（UFS）与根卷 | [UFS 存储](/zh/porting/ufs) | 进行中 | `ShengUfs` 已链接、SMMU 已绑；缺 `native-ufs-root=1` 实跑、SHNGROOT 窗口、`root_match_volume_uuid`；VerifiedRoot 信封 128 MiB > 64 MiB payload |
| 5 | 显示/亮度 | [显示](/zh/porting/display) | 进行中 | 固件 GOP 1216×2688 ✅；**`GopFramebuffer` 在 `[components]` 但不在 `[kernel] archives`，内核里 0 命中**；`DisplayTimingProbe`/`AppleARMBacklight`/`GeniI2c` 不在本地目录；mdss/DSI 的 DT 已入库 |
| 6 | UEFI 按键 | [触控与按键](/zh/porting/input) | 阻塞 | TLMM 读探测：`0x0F000000`/`0x0F00FFF0` 可读，`0x0F010000` 起读崩；需 DxeCore/ArmMmuLib 建映射或改走 PMIC/SPMI |
| 7 | 触摸 | [触控与按键](/zh/porting/input) | 未开始 | 面板触控是 **Synaptics TCM**（SPI19 @ `1a80000`，reset GPIO `<0x33 0x42 0x00>`，见 `HARDWARE-INVENTORY.md`），**不是 Novatek**；目录里只有 `QcomGeniSpi`/`ShengNovatekTouch`/`ShengHidInput`，无 TCM 驱动 → 需自写或改通用路径 |
| 8 | GPU/Metal | [GPU 与 Metal](/zh/porting/gpu) | 未开始 | 目录 `Adreno740`/`AdrenoMetal` 面向 SM8550/A740，SM8850 需对应实现 |
| 9 | Wi-Fi/蓝牙 | [Wi-Fi](/zh/porting/network) | 未开始 | 确认模组型号（目录参考 WCN7850） |
| 10 | USB/基带/音频/电源 | [USB](/zh/porting/usb)、[电源](/zh/porting/power) | 未开始 | 按驱动顺序排在存储/显示之后 |
| 11 | 专有文件哈希 | [设备仓库](/zh/porting/device-repository) | ✅ 完成 | 10 项全部由本地 9008 dump 回填 |
| 12 | userdata 双系统分区 | [双系统分区](/zh/porting/dual-system-partition) | 进行中 | `board.toml [userdata]` 的 `first_lba/last_lba/lba_bytes` 已按实测填入；缺 SHNGROOT 镜像与窗口表 |
| 13 | 记录完成等级 | [适配记录](/zh/porting/records) | 进行中 | `firmware`/`display` 为 device-verified；`storage`/`input` 仍 planned，其余按证据升级 |
| 14 | 打包脚本 | [设备打包脚本](/zh/porting/packaging) | 进行中 | `scripts/package.sh` 需在干净检出跑通并记录产物 |
| 15 | 发布 | [设备仓库 §发布](/zh/porting/device-repository#publish) | 不发布 | owner 决定保持私有：不加 `unixnu` topic、不生成官网设备页、不启用 Actions |
| 16 | 固件日志导出（QR） | [日志抓取](/zh/porting/log-capture) | 进行中 | AdvancedLogDxe 已驻留且 PCD/库已映射；需启用 Advanced Logger 采集模块（会切换 DxeCore 调试通道） |

驱动矩阵与构建接入核对见 [STEP6-DRIVERS-MATRIX.md](STEP6-DRIVERS-MATRIX.md)。

## 刷机方式（本机）

走 efisp 路线，**不写 boot_a**（ABL 拒绝第三方 boot.img），向 `rawdump` 写两段：

```powershell
powershell -File E:\default-workspace\z80u-collect\wsl\flash-rawdump-9008.ps1 `
  -Payload <payload.img> -Fd <SILICIUM_UEFI.fd>
```

payload → `rawdump+0`（LBA 4743976），FD → `rawdump+64 MiB`（LBA 4760360）；
判读见 [BOOT-FIRST.md](BOOT-FIRST.md)，启动链见 [BOOT-CHAIN.md](BOOT-CHAIN.md)。
