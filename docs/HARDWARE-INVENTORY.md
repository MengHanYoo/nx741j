# 硬件清单与缺失输入（目标设备实测）

本文件对应 `reference/` 第 1 项"收集设备输入"。每条都注明来源；寄存器地址与分区布局均取自**目标设备**
（stock DT、9008 rawprogram、运行中的 Android），不引用其它机型。

## 1. 硬件清单

| 项目 | 取值 | 来源 |
|---|---|---|
| SoC | SM8850（Kaanapali，代号 canoe） | `adb shell getprop ro.soc.model` → `SM8850`；`ro.board.platform` → `canoe` |
| 机型 | NX741J / `2512BPNDAC`，Android 17，构建 `CP2A.260605.016` | `getprop ro.product.model`、`ro.build.display.id` |
| 目标 OS | iOS 16.7.16 (20H392)，XNU 8796.141.3，arm64，4 KiB 页 | `board.toml` / `porting.toml` `[device]` |
| CPU 拓扑 | 8 核 6+2：cluster0 `cpu@0…500`（6 核）、cluster1 `cpu@10000,10100`（2 核） | stock DT（`dts/board.dts` cpu-map）+ 运行中 Android |
| CPU 标识/频率 | MIDR `0x51`/`0x002`（Oryon）；3.63 GHz(policy0) / 4.61 GHz(policy6) | 运行中的 Android（cpufreq）；stock DT 无频率表与 MIDR |
| 中断控制器 | GICv3（distributor `0x17000000`）+ ARM generic timer（19.2 MHz） | stock DT / `docs/SHARED-SUPPORT.md` |
| 内存 | DDR 基址 `0x80000000`；**51 个保留区** | `porting.toml` `memory` 区、`MemoryMapLib.c` |
| SMEM | `0x81D00000` / `0x200000`（XBL 初始化） | XBL DT；DXE 读取仍为 0（见缺失项 3） |
| FD 窗口 | `0xFF300000` / `0x400000` | efisp loader 实测：内存映射类型 `0x7`（Conventional）、1024 页 |
| DXE 堆 | `0xB00F0000` / `0x131A0000` | `porting.toml` `firmware` 区 |
| 存储 | UFS **KLUGG8NHKB** | `porting.toml` `storage` 区 |
| 分区布局 | GPT；关键 LBA（4096 B 扇区）：`boot_a` 119078、`efisp` 748894、`rawdump` 4743976、`userdata` 4875048、`misc` 8200、`persist` 8 | 10.24 备份 `rawprogram0–5.xml` + 设备 GPT（9008 读回校验） |
| 面板 | `bf375` / `rm692h5`，原生 1440×3200；UEFI GOP 输出 1216×2688 | stock DT + 固件（`PcdFrameBufferWidth/Height`） |
| 触控 | **Synaptics TCM**，SPI19 @ `1a80000`（`qupv3_4`，GENI SE `1ac0000`）；reset GPIO `<0x33 0x42 0x00>` | Android `/sys/class/input/event6` 路径 + `docs/DRIVER-CHECKLIST.md` |
| 无线 | **WCN7850/7851**，PCIe `pcie@1c00000`，domain 0，BAR `0x40200000`；DT 节点 `qcom,cnss-peach`、`cnss_wlan_region`（32 MB DMA）、`qcom,wcn-bt` | stock DT + `docs/DRIVER-CHECKLIST.md`；运行中 `wlan0`、PCIe `0000:01:00.0` |
| GPU | Adreno 840（gen80200） | stock DT / `porting.toml` `gpu` 区 |
| 固件版本 | 13 项，见 `porting.toml` `firmware` 区；FD 当前构建见 `docs/ANALYSIS-EFISP-VENDORBOOT.md` | 构建日志 + `Guid.xref` |

## 2. 缺失输入（下一项要测的东西）

> 2026-11-07 补实测：第 4、5、7 项已从 **stock DTB** 读出并落实（见第 2.1 节），
> 仍缺的是 1、2、3、6 四项（都需要设备侧或驱动代码工作）。

| # | 缺失项 | 为什么要 | 下一步测量 | 状态 |
|---|---|---|---|---|
| 1 | `DTBExtnProtocol` 的**提供者** | `HALIOMMUDxe` 因它缺失而失败，进而 UFS 拿不到 IOMMU | 在 stock 180 个驱动里按"谁安装该 GUID"定位；候选：`10e193df`/`1c5705b3`/`04de8591`/`8c102bba`/`7a32bd23`/`8e9bd160` 等未命名驱动 | ❌ 未解决 |
| 2 | Mu DXE 里 **UFS → BlockIo** 链路 | 抓日志/读分区的唯一通道 | 逐协议排查 `UFSDxe` 依赖：`HWIO`→`NPA`→`Qseecom/SCM`→`TzeLoader`→`HALIOMMU`→`RPMB` | ❌ 未解决 |
| 3 | SMEM 读到 0 | DALSYS 依赖 | stock DT 已确认 `smem_region@81d00000`（`0x200000`、`no-map`、hwlocks）；下一步对比 ABL 侧是否已填充/清空 | 🟡 部分 |
| 4 | 触控中断 GPIO | 触控驱动需要 IRQ | **已读出**（见 2.1） | ✅ |
| 5 | WCN7850 board_id / PCI id | `Ath12kWifi` ID 表匹配 | **PCI id `0x110e` 已读出**；IMEM board_id（imem 偏移 `0x60`）仍需运行时读取 | ✅/🟡 |
| 6 | FD 版本 bisect | 历史上 `2f8bc430` 能到面板、`db122c8a` 不行 | 逐个重建并上机拍屏 | ❌ 未解决（设备侧） |
| 7 | GIC 全布局 | XNU/ArmCpu 需要 | **已读出**（见 2.1） | ✅ |

### 2.1 本轮从 stock DTB 补出的值

来源：`Platforms/ZTE/nx741jPkg/FdtBlob/kaanapali-zte-nx741j.dtb`（911,287 B，`dtc` 反编译 42,697 行）。

| 项 | 值（stock DTS 原文） |
|---|---|
| 触控 | `synaptics_tcm@0`，`compatible = "synaptics,tcm-spi"`，`spi-max-frequency = 0x989680`（10 MHz） |
| 触控 IRQ | `interrupts = <0x9e 0x2008>`，`interrupt-parent = <0x33>`（TLMM）；`synaptics,irq-gpio = <0x33 0x9e 0x2008>` → **GPIO 158**，flags `0x2008` |
| 触控 reset/power | `synaptics,reset-gpio = <0x33 0x42 0x00>`（GPIO 66）；`avdd-supply`/`vdd-supply` 各一 |
| GIC | `compatible = "arm,gic-v3"`，dist `0x17000000`/`0x10000`，**redistributor `0x17080000`/`0x200000`**（`#redistributor-regions = <1>`、`redistributor-stride = 0x40000`），ITS `0x17040000`/`0x20000`，PPI `<1 9 4>` |
| PDC | `interrupt-controller@b220000`（`qcom,canoe-pdc`，`0xb220000`/`0x10000` + `0x179600f0`/`0xf4`） |
| WCN（Wi-Fi/BT） | `qcom,cnss-peach@b0000000`，`reg = <0xb0000000 0x10000>`，**`supported-ids = <0x110e>`**（PCI device id），`wlan-en-gpio = <0x33 0x10>`、`sw-ctrl = <0x33 0x12>`、`bt-en-gpio = <0x72f 0x05>` |
| WCN board_id | IMEM `board_id@60`（`qcom,msm-imem-board-id`，reg `<0x60 0x04>`）——运行时读 IMEM 才知值 |
| SMEM | `smem_region@81d00000`，`0x200000`，`no-map`，`hwlocks = <0x18 0x03>` |
| 相邻保留区 | `pdp_ns_shared@81f00000`(1 MiB)、`cpucp_scandump@82000000`(3.5 MiB)、`adsp_mhi@82380000`(128 KiB) |

## 3. 采集工具（主机侧，已就绪）

| 工具 | 用途 | 位置 |
|---|---|---|
| `adb` / `fastboot` | 设备观测与 fastbootd 操作 | `E:\default-workspace\tools\platform-tools` |
| `dtc` / `fdtput` / `fdtget` | stock DT 解析与 FDT 构造验证 | WSL |
| `mkbootimg.py` | boot.img 打包 | `/opt/work/uefi-sheng-main/Resources/Scripts` |
| `clang` + `lld-link` | efisp loader（PE32+ EFI）构建 | `/usr/bin` |
| `QSaharaServer` / `qcedlcmdhelper` / `fh_loader` | 9008 裸写与读回 | `E:\default-workspace\_tmp\edl-tools` |
| `read-9008.ps1` / `restore-9008.ps1` | 分区读取、全量恢复 | `E:\default-workspace\z80u-collect\wsl` |

## 4. 与记录的关系

- 结构化记录：`porting.toml` 的 14 个 area（`configuration`/`firmware`/`cpu`/`memory`/`storage`/`display`/
  `input`/`gpu`/`network`/`audio`/`power`/`packaging`/`diagnostics`/`security`）；
- 驱动适配清单：`docs/DRIVER-CHECKLIST.md`（子系统接口、源码参考、构建接入步骤、设备 package 内服务）；
- 本文件补齐"硬件清单 + 缺失输入列表"这一交付物，缺失项会随测量逐条划掉。
