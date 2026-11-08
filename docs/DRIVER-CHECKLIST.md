# NX741J 驱动适配清单（按官方 drivers 页）

硬件参数与 `dts/nx741j-hw.dtsi`、`docs/DTB-PARAMETERS.md` 同源（实机 stock FDT）。

依据 <https://unixnu.com/zh/porting/drivers> 的 8 级硬件依赖顺序与"接口五要素"
（硬件 / 传输 / 内存 / OS / 构建）。设备：SM8850 "canoe"，数据来自实机 DTB、vendor_boot ramdisk
与 Android 运行态（见 `HARDWARE-INVENTORY.md`）。设备仓库 revision `638b6517ea3ec4f2a75f8f06552332e0a17b120c`。

## 总表

| 顺序 | 能力 | 官方依赖组件 | 本机实现 | 状态 |
|---|---|---|---|---|
| 1 | CPU/中断/定时器/RAM/DMA | ArmDTPlatform、ArmCpu、ArmSmmu4K | ArmDTPlatform + ArmCpu（已选/归档） | 🟡 组件就绪；SMMU 未选 |
| 2 | 存储/根卷/持久 Data | ShengUfs、RamBlock、VirtioMmioBlock、VerifiedRoot | 已选中并链接；ADT 导出 ufshc | 🟡 运行时 fail-closed |
| 3 | 帧缓冲/刷新/亮度 | GopFramebuffer、DisplayTimingProbe、AppleARMBacklight、GeniI2c | 仅 GopFramebuffer | 🟡 |
| 4 | 触控与按键 | QcomGeniSpi、ShengHidInput、ShengNovatekTouch、ButtonProbe | 型号已确认（Synaptics TCM），驱动未写 | ⬜ |
| 5 | GPU/Metal | Adreno740、AdrenoMetal、Sm8550Platform、AdrenoFirmware、AdrenoPM4、AirNir | 未开始 | ⬜ |
| 6 | Wi-Fi/蓝牙/USB | Ath12kWifi、QcaBluetooth、QcomDwc3 | WCN7880 已确认，组件待适配 | ⬜ |
| 6a | 基带 | QcomBaseband | 未开始 | ⬜ |
| 7 | DSP/音频/电池/调频/电源 | AdspProbe、AudioProbe、ShengBattery、CpuFreqProbe、ShengPlatformPower | 未开始（codec WSA884x 已识别） | ⬜ |
| 8 | OS 服务与打包 | SoftwareAES、AppleKeyStoreState、XnuPrelink、DarwinBoot、ShengPreflight | SoftwareAES/XnuPrelink/DarwinBoot 已选 | 🟡 |

## 1. 平台与内存（已选）

| 接口 | 内容 |
|---|---|
| 硬件 | 8 核 MPIDR、PSCI `smc`、GICv3 dist `0x17000000+0x10000` / redist `0x17080000+0x200000` stride `0x40000`、19.2 MHz timer |
| 传输 | 无请求队列；启核/IPI 走 PSCI |
| 内存 | RAM 21 段合计 15.24 GiB；ADT 单段 `0x8C0000000+5 GiB`；`dram_size=0x140000000` |
| OS | `ArmDTPlatform`（arm-io 平台专家）+ `ArmCpu`；`CONFIG_XNU_ARM_DT_PLATFORM/ARMCPU` |
| 构建 | arch profile 的 `[kernel].archives` 含 `component:ArmDTPlatform`、`component:ArmCpu`；已用 `llvm-ar`/`llvm-nm` 验证 |

## 2. 存储 UFS（阻塞）

| 接口 | 内容 |
|---|---|
| 硬件 | `ufshc@1d84000`（`reg` 含 ice/mcq 区）、`ufsphy_mem@1d80000`（`qmp-v4-canoe`）、GCC `0x100000`、SMMU `apps-smmu@15000000` stream `0x60`、SPI `0x109` |
| 传输 | UFS 请求队列 + MCQ 区（实机 DTB 提供 `mcq_sqd/mcq_vs`） |
| 内存 | `dma-coherent`、`iommu-dma="fastmap"`、ICE 区 |
| OS | `ShengUfs`（`NativeUfs`，类 `NativeUfsBlock : IOBlockStorageDevice`） |
| 构建 | 本地覆盖 `ShengUfs-canoe` 已改 `GccUfs.h`：AHB `0x77028`、UNIPRO `0x7706c`、ICE `0x7707c`、PHY_AUX `0x770bc`、RX0 `0x77030`、RX1 `0x770d8`、TX0 `0x7702c`、AGGRE `0x770f0`；缺 `IOBlockStorageDevice` 基类与 `_vr_*`/`_boot_progress_poll` 归档 |

板级输入：UFS `SAMSUNG KLUGG8NHKB-F0H1` FW `0500`，`0xED4F800` 块（≈1 TB），LUN 掩码 `0x3f`；
`[userdata]` 已按 GPT 填 `first_lba=4875048 / last_lba=248838138 / lba_bytes=4096`。

## 3. 显示（部分）

| 接口 | 内容 |
|---|---|
| 硬件 | 面板 `bf375` + DDIC `rm692h5`；GOP 1216×2688 32bpp |
| 传输 | DSI（内核侧需 DPU/DSI 驱动，尚未适配） |
| 内存 | `splash_region` 43 MB（实机 DTB） |
| OS | `GopFramebuffer`（已选）；亮度未选 `AppleARMBacklight` |
| 构建 | `GopFramebuffer` 为 embedded 阶段，已随组件解析 |

## 4. 触控与按键（未开始）

| 接口 | 内容 |
|---|---|
| 硬件 | **Synaptics TCM**（`synaptics_tcm_touch`，10 指，ABS 0–12159 / 0–26879）；SPI + reset GPIO |
| 传输 | SPI `spi@1a80000`（GENI，IRQ 856，50 MHz）；TCM 报告协议待实现 |
| 内存 | 无 DMA 窗口需求（SPI 小缓冲） |
| OS | 复用 `ShengHidInput` 的 IOHIDDevice；`ShengNovatekTouch` 不适用 |
| 构建 | 需新建 Synaptics TCM 组件或板级 package 驱动；`QcomGeniSpi`/`ShengHidInput` 加入 include 后再接 archives |

板级输入：`syna_firmware.img`（`/vendor/firmware`，241,578 B，sha256 `f6a68725…`）；
指纹 `goodix_fp`、接近/ALS `vi530x`（`VI5300-M40…bin`）。UEFI 按键另有 TLMM 映射阻塞。

## 5. GPU / 6. 无线 / 6a. 基带 / 7. 音频与电源

| 顺序 | 已确认输入 | 状态 |
|---|---|---|
| GPU | 实机 DTB：`qcom,adreno-gpu-gen8-2-1`，`Adreno840v2`，chipid `0x44050a01`，reg `0x3d00000+0x40000`，IRQ 300/80；目录 A740 需新实现 | ⬜ |
| Wi-Fi/BT | **WCN7880**（cnss `0xb0000000+0x10000`，PCIe `1c00000` BAR `0x40200000`）；BT 固件 `BTFW.BRAHMA.2.0.1-00181` / `BTFW.GANGES.2.0.1-00184`；WLAN board data `/IMAGE/PEACH/BDWLAN.E01…E49`；`WCNSS_qcom_cfg.ini`；MAC `5C:7D:AE:43:D9:F8` | ⬜ |
| 基带 | modem 镜像内 `MODEM_OE/KAANAPAL` 构建；baseband `EN_PACK-2.124266.53` | ⬜ |
| USB | `ssusb@a600000/dwc3@a600000`，reg `0xa600000+0xd93c`，IRQ 133，SMMU stream `0x40` | ⬜ |
| 音频 | codec `wsa884x`（`canoe-mtp-wsa884x-snd-card`）；`mixer_paths_canoe_mtp_wsa884x.xml` | ⬜ |

## 8. OS 服务

| 组件 | 状态 |
|---|---|
| `SoftwareAES` | ✅ 已选并归档（`libSoftwareAES.a`） |
| `XnuPrelink` | ✅ 已跑通（11 组件 / 23,833 bindings，`runtime_qualified=false`） |
| `DarwinBoot` | ✅ 固件内 loader（EL1） |
| `AppleKeyStoreState` | ⬜ 未选（用户态/安全阶段再启用） |
| `ShengPreflight` | ⬜ 未选（参考实现面向小米平板） |

## 构建接入核对（官方第 3 节）

```sh
./unixnu status --json -D ../UniXNU-nx741j   # components 6, modules 20, patches 63
./unixnu kernel -D ../UniXNU-nx741j          # kernel.macho sha256 d082e1f7…
./unixnu payload -D ../UniXNU-nx741j         # payload.img 64 MiB
./unixnu bootimg -D ../UniXNU-nx741j         # boot.img 68,677,632 B
llvm-ar t build/armcpu/libArmCpu.a            # 归档内容核对
```

## 存疑

| 项 | 说明 |
|---|---|
| storage ABI | 需 iOS 16 storage policy 脚本或 xnu-8796 storage patch（见 STEP6-UFS） |
| 触摸/Wi-Fi 驱动 | 只有型号与固件，协议层未实现 |
| UEFI 按键 | TLMM 仅映射 64 KiB，按键引脚不可读 |
| 预链接资格 | `runtime_qualified/abi_qualified=false`，需实机启动 |
