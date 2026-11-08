# 硬件清单与缺失输入（目标设备实测）

本文件对应 `reference/` 第 1 项"收集设备输入"。每条都注明来源；寄存器地址与分区布局均取自**目标设备**
（stock DT、9008 rawprogram、运行中的 Android），不引用其它机型。

## 内存（实机实测）

`MemTotal: 15600240 kB`（约 14.88 GiB，16 GB 物料）；实机 DTB 的 `memory` 节点为 21 段、
合计 `0x3CF4F8000`，最大连续段 `0x8C0000000 + 5 GiB`。完整区段表见
[STEP6-PLATFORM.md](STEP6-PLATFORM.md#内存与-dma)。

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

## 5. 步骤 1 复核（2026-11-08，对照官方 porting/steps）

官方交付物 = 主机工具 + 硬件清单 + 缺失输入列表 + 移植记录入口。当前状态：

| 官方要求 | 状态 | 证据/缺口 |
|---|---|---|
| 主机工具就绪 | ✅ | 第 3 节（adb/fastboot、dtc、9008 工具、clang/lld） |
| SoC / CPU 拓扑 / RAM 保留区 / 中断控制器 | ✅ | 第 1 节；51 个保留区见 `porting.toml`；GIC 全布局见 2.1 |
| 存储 / 面板 / 触控 / 无线 / 固件版本 | ✅ | 第 1 节；触控 Synaptics TCM（SPI19），WCN7850（PCIe） |
| 寄存器地址与分区布局取自目标设备 | ✅ | stock DT + 9008 rawprogram + 设备 GPT |
| 缺失输入列表 | ✅ | 第 2 节，含每项"下一步测量" |
| 移植记录入口 | ✅ | `porting.toml`（14 个 area）+ `docs/PORTING-RECORD.md` + `docs/DRIVER-CHECKLIST.md` |

### 5.1 本轮新增/修正的输入

| 项目 | 值 | 来源 |
|---|---|---|
| 实体按键（VOL+） | TLMM GPIO 101，active-low，pull-up | stock DTS `gpio_keys/vol_up` |
| 实体按键（FOCUS / SHOOT） | TLMM GPIO 179 / 95，active-low，bias-disable | stock DTS `focus_key`/`shoot_key` |
| 实体按键（VOL−） | PMIC `pmk8350-pon / resin`（linux,code 0x72），非 GPIO | stock DTS |
| TLMM 控制器 | `qcom,canoe-tlmm`，base `0x0F000000`，DTB 声明 **0x202000** | stock DTS `pinctrl@f000000` |
| TLMM 固件映射 | `MemoryMapLib.c` 声明 `{"TLMM",0x0F000000,0x01000000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE}`（16 MiB） | 平台源码 |
| TLMM 实际可读性 | 读 `0x0F010000` 触发异常；`0x65000/0xB3000/0x5F000` 读取结果以 V41 探针为准 | 实机（V22/V31/V41） |
| 按键厂商栈 | `SPMIDxe/TLMMDxe/PmicDxeLa/ButtonsDxe/KeypadDxe` 在 Binaries 中齐全，但 `PmicDxeLa` 需要 RPMH→RSC/SCM，而 SCM 提供者 `ScmDxeCompat` 与 `TzDxeLA` 是同一二进制，恢复即触发 `TzLoaderDxe.c:1150` 断言 | 实机 V38b/V39c + 源码/字符串 |

### 5.2 仍缺的输入（更新）

| # | 缺失项 | 状态 | 说明 |
|---|---|---|---|
| 1 | `DTBExtnProtocol` 提供者 | ❌ 确认不存在 | 已扫 `Binaries/nx741j` 180 个驱动：只有使用方、无安装方；影响 `DALTLMM`/`PmicDxe`/`RngDxe`/`UCDxe`/`HALIOMMU` |
| 2 | Mu DXE 的 UFS→BlockIo | ❌（已绕过） | payload 卷走 `UniXNURamDiskDxe` + x0 FDT，官方链不依赖 UFS；UFS 仅影响固件内日志导出等可选功能 |
| 3 | SMEM 内容读 0 | 🟡 | V39c 中 `DALSys.efi` 已启动（`SmemStub: item 404 -> …`），协议可用；数据内容未核对 |
| 4 | WCN7850 IMEM board_id | 🟡 | PCI id `0x110e` 已确认；board_id 需运行时读 IMEM |
| 5 | FD 版本 bisect | ✅ 已淘汰 | 改为分阶段最小 FV（V15…V41），问题定位不再依赖 FD 盲测 |
| 6 | 音频 codec / DSP 固件 | ✅ 已登记 | 见第 6 节（WCD939x、LPASS CDC v7、ADSP `adsp.mdt`） |
| 7 | USB 控制器与 PHY 参数 | ✅ 已登记 | 见第 6.2 节（ssusb@a600000、eUSB2/SS PHY、repeater） |
| 8 | 基带/调制解调器 | ✅ 已登记 | 见第 6.3 节（modem/cdsp/spss/soccp、固件名） |

**结论**：步骤 1 的交付物（清单 + 缺失列表 + 工具 + 记录入口）已具备；仍缺的输入集中在
"厂商协议 provider（DTBExtn）"与"音频/USB/基带登记"，不影响当前阶段 2/3 的推进。

## 6. 音频 / USB / 基带登记（关闭 §5.2 第 6 项，2026-11-08）

来源均为本机 stock DTS（`Platforms/ZTE/nx741jPkg/FdtBlob/kaanapali-zte-nx741j.dtb`，
反编译 42,697 行）与运行中 Android 的节点路径；未引用其它机型。

### 6.1 音频

| 项目 | 值 | DTS 位置 |
|---|---|---|
| 音频 codec | **WCD939x**（`qcom,wcd939x-i2c`，I2C 地址 `0x0e`，含 USB-C/模式切换 `mode-switch`） | `wcd939x_i2c@e` |
| codec 中断 | PMIC 侧 `interrupts = <0x00 0xb6 0x01 0x03>`，`interrupt-names = "usb_wcd"` | 同上 |
| LPASS CDC | `qcom,lpass-cdc`，**version 7**，含 clock RSC manager | `lpass-cdc` |
| WSA 宏（扬声器） | `wsa-macro@6B00000`，SWR `swr0` | `lpass-cdc` |
| WSA2 宏（第二路） | `wsa2-macro@6AA0000`，SWR `swr3` | 同上 |
| VA 宏（语音） | `va-macro@7660000`，SWR `swr2` | 同上 |
| RX/TX 宏 | `rx-macro@6AC0000`（`swr1`）、`tx-macro@6AD0000` | 同上 |
| 蓝牙 SWR | `lpass_bt_swr@6CA0000`（`swr4`） | 同上 |
| ADSP | `remoteproc-adsp@03000000`（`qcom,canoe-adsp-pas`），reg `0x03000000/0x10000`，固件 **`adsp.mdt` + `adsp_dtb.mdt`** | `remoteproc-adsp@03000000` |
| ADSP 辅助 | `qcom,msm-adsp-loader`、`qcom,adsp-notify`、`adsp-sleepmon`、smp2p-adsp | 对应节点 |

### 6.2 USB

| 项目 | 值 | DTS 位置 |
|---|---|---|
| 控制器 | `ssusb@a600000`（`qcom,dwc-usb3-msm`），`core_base 0xA600000/0x100000`，`tcsr_dyn_en_dis 0x1FC6000/0x4` | `ssusb@a600000` |
| 角色 | `usb-role-switch`、`qcom,use-eusb2-phy`、PDC 中断 | 同上 |
| 时钟 | core `200 MHz`（`0xbebc200`）、HS `~66.6 MHz`（`0x3f940ab`）、disconnected `~133.3 MHz`（`0x7f28155`） | 同上 |
| 中断 | `pwr_event_irq` GIC `0x82` SPI；`dp_hs_phy_irq`/`dm_hs_phy_irq`/`ss_phy_irq` 走 PDC | 同上 |
| eUSB2 PHY | `hsphy@88e3000`（`qcom,usb-m31-eusb2-phy`），`0x88E3000/0x29C`；EUD `0x88E2000/4`、`0xC278000/4` | `hsphy@88e3000` |
| SS/DP PHY | `ssphy@88e8000`（`qcom,usb-ssphy-qmp-dp-combo`），`0x88E8000/0x3000` | `ssphy@88e8000` |
| eUSB2 repeater | `eusb2-repeater@fd00`（PMIC 侧 `qcom,pmic-eusb2-repeater`） | PMIC 节点内 |

### 6.3 基带 / 协处理器

| 项目 | 值 | DTS 位置 |
|---|---|---|
| Modem | `remoteproc-mss@04080000`（`qcom,canoe-modem-pas`），reg `0x04080000/0x10000`，固件 **`modem.mdt` + `modem_dtb.mdt`** | `remoteproc-mss@04080000` |
| MPSS 保留区 | `mpss_region@8aa00000`（内存保留）、`q6_mpss_dtb_region@99500000` | 同名节点 |
| CDSP | `remoteproc-cdsp@32300000`，固件 `cdsp.mdt` + `cdsp_dtb.mdt` | 同名节点 |
| SPSS | `remoteproc-spss@1880000`（`qcom,canoe-spss-pas`） | 同名节点 |
| SOCCP | `remoteproc-soccp@a3380000`，固件 `soccp.mbn` + `soccp_dtb.mbn` | 同名节点 |
| 通信 | `qcom,smp2p-modem`、`qcom,glink-label = "mpss"`、`qcom,qrtr-mhi-cnss` | 对应节点 |

### 6.4 状态更新

- §5.2 第 6 项（音频 codec/DSP、USB 控制器与 PHY、基带）→ **✅ 已登记**；
- 本节只登记"输入"，不含驱动实现：音频/USB/基带分别按官方步骤 6 的 DSP/音频/电源与 USB 章节推进；
- 固件文件（`adsp.mdt`、`modem.mdt` 等）需在 `proprietary-files.txt` 中补对应来源，属步骤 3 收尾项。

## 实机输入采集（f5a4e36）

| 类别 | 实测 | 来源命令 |
|---|---|---|
| 触摸 | `synaptics_tcm_touch`（Synaptics TCM，10 指，ABS_X 0–12159 / ABS_Y 0–26879） | `adb shell getevent -pl` |
| 指纹 | `goodix_fp` | 同上 |
| 环境光/接近 | `vi530x` | 同上 |
| 面板 | `bf375` + `rm692h5`，`msm_drm.dsi_display0=qcom,mdss_dsi_bf375_rm692h5_6p8_dev0_dsc_cmd` | 实机 DTB chosen bootargs |
| Wi-Fi/BT | `WCN7861`（`ro.vendor.feature.board_wlan_bt_chip=WCN7861`），PCI `0000:01:00.0` | `adb shell getprop` |
| 存储 | UFS `KLUGG8NHKB`，boot device `soc/1d84000.ufshc`，PHY `1d80000.ufsphy_mem` | stock 记录 + `getprop ro.boot.boot_devices` |
| 固件 | build `CP2A.260605.016`，baseband `EN_PACK-2.124266.53`，fingerprint `Xiaomi/nezha/nezha:17/...` | `adb shell getprop` |
| SoC 序列 | `160370627`（`0x098EE0C3`，QFPROM） | `/sys/devices/soc0/serial_number` |

## vendor 构建与固件输入（5d07caa）

| 项 | 值 | 来源 |
|---|---|---|
| vendor build | `BQ2A.250705.001-BP2A.250605.031.A3` | `getprop ro.vendor.build.id` |
| vendor fingerprint | `Xiaomi/mivendor/mivendor:16/...` | `getprop ro.vendor.build.fingerprint` |
| Wi-Fi 配置 | `/vendor/etc/wifi/peach_v2/WCNSS_qcom_cfg.ini`（ZTE 定制注释） | `adb shell ls/head` |
| Wi-Fi/BT 模块 | `cnss2.ko`、`icnss2.ko`、`wlan_firmware_service.ko`、`qbt_handler.ko`、`qti_btl.ko`、`lpass_bt_swr_dlkm.ko` | `/vendor/lib/modules` |
| 显示校准 | `/vendor/etc/display/backlight_calib_*vtdr6130*`、`qdcm_calib_data_*` | `adb shell find /vendor/etc/display` |
| 存储容量 | `dumpsys diskstats` 约 975,850,316 KiB（≈930 GiB 可用，1 TB 物料） | `adb shell dumpsys diskstats` |
| 触摸/传感器节点 | `synaptics_tcm_touch`、`goodix_fp`、`vi530x` | `getevent -pl` |

## 仍缺（root / 9008 才能取）

| 项 | 原因 | 下一步 |
|---|---|---|
| UFS 具体型号字符串 | `/sys/block/*/device/model`、`/proc/partitions` 被 SELinux 拒绝 | 9008 下 `fh_loader --getstorageinfo` 或 root 读取 |
| Synaptics TCM 固件与配置 | `/vendor/firmware_mnt`、`/odm` 无对应公开文件 | root 读 `/vendor/firmware_mnt/image` 或 vendor 分区 |
| WCN7861 固件文件名/蓝牙 UART | `/vendor/firmware_mnt` 拒绝访问 | root 或 vendor 分区提取 |

## 无线与触摸固件（9008 分区提取，3336080）

| 项 | 值 | 来源 |
|---|---|---|
| Wi-Fi/BT 芯片 | **WCN7880**（`phyAniDynamicOffset_wcn7880` 等符号），不是属性里的 WCN7861 | `modem_a` FAT 内字符串 |
| BT 固件 | `BTFW.BRAHMA.2.0.1-00181-PATCHZ-1_DEF`、`BTFW.GANGES.2.0.1-00184-PATCHZ-1_DEF` | `bluetooth_a` FAT `/IMAGE/*.VER` |
| BT MBN | `BRHBTFW2.MBN`(291,532 B)、`GNGBTFW2.MBN`(291,532 B)、`GNGBTFW2.TLV`(278,392 B)、`BRHBTFW2.TLV` | 同上 |
| Wi-Fi board data | `/IMAGE/PEACH/BDWLAN.E01`…`E49`（72 个，125,420 B）+ `BDWLAN.ELF`（125,264 B） | `modem_a` FAT（`modem_a` sha256 `abf6f10dc0b681c2`） |
| Wi-Fi 配置 | `/vendor/etc/wifi/peach_v2/WCNSS_qcom_cfg.ini` | Android |
| 触摸 TCM | `sdc_code_tcm.bin`、`sdc_data_tcm.bin` 名称出现在 modem 镜像字符串中，但不在其 FAT 树内 | 待 super/vendor 提取 |

提取命令：`read-9008.ps1` dump `bluetooth_a`(LBA 95238,2048) / `modem_a`(LBA 5638,89600)，再用 FAT12/FAT16 解析脚本读取。

## vendor_boot ramdisk 模块（9008 提取，3ad4178）

`vendor_boot_a`（LBA 302310，96 MiB）解包后为 31,204,732 B 的 cpio（LZ4 legacy），含 336 个
`lib/modules/*.ko`，与本设备相关的有：

| 模块 | 用途 |
|---|---|
| `gcc-canoe.ko` | GCC 时钟，含 UFS 分支偏移（见 STEP6-UFS.md 表） |
| `phy-qcom-ufs-qmp-v4-canoe.ko` | UFS PHY（canoe 变体） |
| `ufs-qcom.ko` / `ufshcd-crypto-qti.ko` | UFS 控制器与 ICE |
| `gcc-alor.ko`、`camcc-canoe.ko`、`dispcc-canoe.ko`、`gpucc-canoe.ko`、`qnoc-canoe.ko`、`pinctrl-canoe.ko` | 其余 GCC/互联/引脚控制器 |

触摸 TCM 固件（`sdc_code_tcm.bin`/`sdc_data_tcm.bin`）不在该 ramdisk 中，需 vendor 镜像。

## UFS 与触摸固件（9008 提取，8ad1d6d）

| 项 | 值 |
|---|---|
| UFS 型号 | `SAMSUNG KLUGG8NHKB-F0H1`，固件版本 `0500`（`fh_loader getstorageinfo` 输出：`UFS Inquiry Command Output: SAMSUNG KLUGG8NHKB-F0H1 0500`） |
| UFS 容量 | `Logical Block Count: 0xED4F800`（≈1 TB），`LUN Enable Bitmask: 0x3f`（6 LUN） |
| Synaptics TCM 固件 | `/vendor/firmware/syna_firmware.img`，241,578 B，sha256 `f6a687255be5bf13dc0bb0f35d28569a1a62c9c20ad0635534732d2f3c24737b`，头部 `BOOT_CONFIG` + `#SYNA CFG$]` |
| 接近/ALS 固件 | `/vendor/firmware_mnt/image/VI5300-M40_G10_R03_V1.18_20231102.bin` |
| 提取方式 | 9008 dump `super_a` → 解析 liblp → 截取 `vendor_a`（EROFS，偏移扇区 12935168）→ mount 后复制 |

## Wi-Fi/BT 固件布局（vendor EROFS 提取，69363db）

| 路径 | 说明 |
|---|---|
| `/vendor/firmware/wlan/qca_cld/peach_v2/WCNSS_qcom_cfg.ini` | 实际文件在 `/vendor/etc/wifi/peach_v2/`，固件目录下是绝对符号链接 |
| `/vendor/etc/wifi/peach_v2/WCNSS_qcom_cfg.ini` | ZTE 定制的 WLAN 配置（已提取，含 `gEnableLpassSupport` 等） |
| `/vendor/firmware/wlan/qca_cld/peach_v2/wlan_mac.bin` | 符号链接到 `/mnt/vendor/persist/peach_v2/wlan_mac.bin`（persist 分区，需 9008 dump persist 提取） |
| `/vendor/firmware/wlanmdsp.otaupdate` | 符号链接到 `/data/vendor/firmware/wlanmdsp.mbn`（运行时生成，boot 镜像内无实体） |
| `modem_a` `/IMAGE/PEACH/BDWLAN.E01…E49` | WLAN board data（72 个，125,420 B） |
| `bluetooth_a` `/IMAGE/*.MBN` | BT 固件 BRAHMA/GANGES |

即：MAC 固件 `wlanmdsp.mbn` 只在设备运行期存在，persist 里的 MAC 需单独 dump persist 分区。

## Wi-Fi MAC（persist 提取，9584183）

9008 dump `persist`（LUN0 LBA 8，8192 扇区 = 32 MiB）→ ext4 挂载 → 取得：

| 项 | 值 |
|---|---|
| 文件 | `persist/wifimac.dat`，39 B，sha256 `8140b5beba299ee817a91600303393a2cec0335379843ec690b63dfbe77eab74` |
| 内容 | `wifiaddr:0x5C 0x7D 0xAE 0x43 0xD9 0xF8` |
| MAC | **5C:7D:AE:43:D9:F8** |

`/vendor/firmware/wlan/qca_cld/peach_v2/wlan_mac.bin` 的符号链接目标
`/mnt/vendor/persist/peach_v2/wlan_mac.bin` 在本机不存在，MAC 实际存放在根目录 `wifimac.dat`。
