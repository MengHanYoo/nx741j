# NX741J 驱动适配清单

依据 <https://unixnu.com/zh/porting/drivers>：每个子系统要链接**接口**、**源码参考**和**构建接入**，
并把设备 package 内的服务计入。本表以 NX741J（SM8850 / canoe / Kaanapali）实机固件与 DT 为准。

- 固件侧驱动库存：`/opt/work/uefi-sheng-main/Platforms/ZTE/nx741jPkg/Include/DXE.inc`（85 个 INF）
- 主仓库可用组件：`UniXNU/components.toml`（27 个）
- 固件侧自建驱动记录：`firmware/DRIVERS.md`（SmemStubDxe、KernelStubDxe）

## 1. 显示（Display / DSI / Panel）

| 项 | 内容 |
|---|---|
| UEFI 驱动 | `DisplayDxe`（厂商二进制，FV 内）+ `SimpleFbDxe`（我们源码，`SiliciumPkg/Drivers`） |
| 接口 | `EFI_GRAPHICS_OUTPUT_PROTOCOL`；模式 **1216×2688**（垂直+水平居中），32 bpp |
| DT 节点 | `mdss` / `dsi` / `panel`（panel = `bf375_rm692h5_6p8_dev0_dsc_cmd`）、`splash_region@FC800000`（43 MB） |
| XNU 组件 | `GopFramebuffer`（27 个组件之一） |
| 源码参考 | Linux `drivers/gpu/drm/msm/dsi/`；面板驱动 `panel-bf375-rm692h5` |
| 构建接入 | UEFI：`DXE.inc` + `APRIORI.inc` + DSC `[Components]`；XNU：`board.toml [components] include = ["GopFramebuffer"]` |
| 状态 | ✅ UEFI 侧已验证（GOP 1216×2688 出图）；XNU 侧未开始 |
| 下一步 | XNU 起 `GopFramebuffer` 并核对 framebuffer 基址与 DT `splash_region` 一致 |

## 2. 存储（UFS）

| 项 | 内容 |
|---|---|
| UEFI 驱动 | `UFSDxe`（厂商二进制）+ `SdccDxe`（SD 卡，未用） |
| 接口 | UEFI `EFI_BLOCK_IO_PROTOCOL` / `EFI_DISK_IO_PROTOCOL`；分区 LBA：`boot_a=119078`、`uefi_a=6`、`efisp=748894` |
| DT 节点 | `ufshc@1d84000`（UFS host controller）、`ufsphy`、`icc` / `rpmh` 供电 |
| XNU 组件 | `ShengUfs`（`NativeUfs.cpp`，SM8550 UFS 传输 + sheng 持久化存储） |
| 源码参考 | Linux `drivers/ufs/host/ufs-qcom.c`；`UniXNU/ShengUfs` |
| 构建接入 | UEFI：FV；XNU：`include = ["ShengUfs"]`，产物 `build/sheng-ufs/libShengUfs.a` 进 `[kernel] archives` |
| 依赖 | `Sm8550Platform`（时钟/SMMU）→ 需移植为 SM8850 版本；`ArmSmmu4K` |
| 状态 | ✅ UEFI 侧 `UFSDxe` 可枚举分区；XNU 侧未开始 |
| 下一步 | SM8850 UFS host 版本/PHY 序列差异核对；`ShengUfs` 的 clock 表换 SM8850 |

## 3. 触控（Touch）

| 项 | 内容 |
|---|---|
| UEFI 驱动 | **无**（厂商 UEFI 不含触控驱动，FV 里只有 `SPIDxe` / `GpiDxe` / `I2CDxe` 总线） |
| DT 节点 | `synaptics_tcm`（SPI）；`reset_gpio = <0x33 0x42 0x00>`；中断 GPIO 待补 |
| XNU 组件 | `ShengNovatekTouch`（**Novatek NT36532**，不适配本机）+ `ShengHidInput`（IOHIDDevice 通路）+ `QcomGeniSpi`（SPI 传输） |
| 源码参考 | Linux `drivers/input/touchscreen/synaptics_tcm*`；`UniXNU/ShengHidInput`、`ShengNovatekTouch` |
| 构建接入 | XNU：`include = ["ShengHidInput", "QcomGeniSpi"]`；触控协议层需新建 `nx741j` 组件或改 Novatek 为 Synaptics TCM |
| 状态 | ⚠️ 未验证（UEFI 无驱动，XNU 组件不匹配） |
| 下一步 | 新建 Synaptics TCM 组件（或移植 `synaptics_tcm.h` 协议），复用 `ShengHidInput` 的 IOHIDDevice 接口 |

## 4. 无线（WiFi / BT）

| 项 | 内容 |
|---|---|
| UEFI 驱动 | **无**（FV 内无 WLAN/BT 驱动） |
| 芯片/总线 | WCN7850/7851（`qcom,cnss-peach@b0000000`）；PCIe `pcie@1c00000`，domain 0，BAR 0x40200000 |
| DT 节点 | `qcom,cnss-peach`、`cnss_wlan_region`（32 MB DMA）、`qcom,wcn-bt` |
| XNU 组件 | `Ath12kWifi`（ath12k 支持 WCN7850）、`QcaBluetooth` |
| 源码参考 | Linux `drivers/net/wireless/ath/ath12k/`；`UniXNU/Ath12kWifi`、`QcaBluetooth` |
| 构建接入 | XNU：`include = ["Ath12kWifi", "QcaBluetooth"]`；PCIe host 需 `Sm8850Platform` 提供 |
| 状态 | ❌ 未开始（不阻塞启动链） |
| 下一步 | 先确认 `Ath12kWifi` 的 WCN7850 PCI ID 表是否含本机 ID（读 `qcom,cnss-peach` 的 `qcom,board_id`） |

## 5. 电源 / PMIC / RPMh

| 项 | 内容 |
|---|---|
| UEFI 驱动 | `RpmhDxe`、`SPMIDxe`、`PmicDxe`、`VcsDxe`、`ClockDxe`、`ICBDxe` |
| 接口 | `EFI_RPMH_PROTOCOL`、`EFI_SPMI_PROTOCOL`、`EFI_PMIC_PROTOCOL` |
| DT 节点 | `rsc@18900000`、`spmi-bus@c400000`、`pmk8850@0` |
| XNU 组件 | `Sm8550Platform`（需 SM8850 版） |
| 状态 | ❌ 固件侧当前卡在 `RpmhDxe`（见归档 §18） |
| 下一步 | 换根（ABL 正常加载）后由 XBL 提供 RPMh 环境，可删掉相关桩 |

## 6. 按钮 / 输入

| 项 | 内容 |
|---|---|
| UEFI 驱动 | `ButtonsDxe`（厂商二进制） |
| 接口 | `EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL`（经 `ConSplitter`） |
| 状态 | ⚠️ 未验证 |
| 下一步 | BDS 起来后验证音量键/电源键导航 |

## 7. GPU

| 项 | 内容 |
|---|---|
| UEFI 驱动 | 无（`Binaries/` 里没有 GPU DXE） |
| 硬件 | Adreno 840（gen80200），`gpu_microcode_region@9959A000` |
| XNU 组件 | `Adreno740`（Adreno 740 / SM8550）→ **需移植到 840**；`AdrenoPM4`、`AdrenoFirmware`、`AdrenoMetal`、`ArmSmmu4K` |
| 源码参考 | Linux `drivers/gpu/drm/msm/adreno/a8xx_gpu.c`（`chip_ids = 0x44050a01`，`ADRENO_8XX_GEN2`） |
| 状态 | ⚠️ 内核侧曾验证（vktest PASS ×35）；UEFI 侧未到 |
| 下一步 | `Adreno740` 的 chip id/固件名（`gen80200_*`）替换；GMEM 18 MiB、`max_slices=3` |

## 8. USB / 音频 / Modem（低优先级）

| 子系统 | UEFI | XNU | 状态 |
|---|---|---|---|
| USB | `XhciDxe` + `Usb*Dxe`（6 个） | 未选组件 | ❌ 未开始 |
| 音频 | `AudioDxe`？（FV 未见） | 未选组件 | ❌ 未开始 |
| Modem | 无（由 XBL 加载） | `QcomBaseband` | ❌ 未开始（`mpss_region` 235 MB） |

## 9. 汇总

| 子系统 | UEFI | XNU 组件 | 可复用 | 状态 |
|---|---|---|---|---|
| 显示 | ✅ | `GopFramebuffer` | 是 | UEFI ✅ / XNU ❌ |
| UFS | ✅ | `ShengUfs` | 需改 SM8850 | UEFI ✅ / XNU ❌ |
| 触控 | 无 | `ShengHidInput` + `QcomGeniSpi` | 协议层需新建 | ⚠️ |
| 无线 | 无 | `Ath12kWifi` + `QcaBluetooth` | 可能可复用 | ❌ |
| 电源 | ⚠️ 卡 Rpmh | `Sm8550Platform` | 需改 SM8850 | ❌ |
| 按钮 | ✅ | — | — | ⚠️ |
| GPU | 无 | `Adreno740` 等 4 个 | 需改 840 | ⚠️ |
| USB/音频/Modem | 部分 | 未选 | — | ❌ |

**结论**：显示与 UFS 的 UEFI 侧已就绪，XNU 侧只差组件选择与 SM8850 适配；
触控与无线需要新组件或协议层改写；电源受固件侧 Rpmh 阻塞，建议与"换根"一起解决。
