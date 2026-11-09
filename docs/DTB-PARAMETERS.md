# 实机 DTB 硬件参数（NX741J / SM8850）

全部取自本机 stock FDT（`stock/dt/live_fdt.dtb`，`soc.toml` 记录其 SHA-256 `a5c109c0…`），
并整理进 `dts/nx741j-hw.dtsi`（`status="disabled"`，仅作板级事实记录）。设备仓库 revision `311bab994e3cc14e565b676d19d3c67580ff7059`。

## UFS

| 参数 | 值 |
|---|---|
| HC | `ufshc@1d84000`，`ufs_mem 0x1d84000+0x3000`、`ice 0x1d88000+0x18000`、`mcq_sqd 0x1da5000+0x2000`、`mcq_vs 0x1da4000+0x10` |
| 中断 | SPI 0x109（265），level-high |
| PHY | `ufsphy_mem@1d80000+0x2000`，`qcom,ufs-phy-qmp-v4-canoe` |
| SMMU | `apps-smmu@15000000`，stream `0x60` |

## 显示

| 参数 | 值 |
|---|---|
| 面板 | `bf375` + DDIC `rm692h5`（bootargs `msm_drm.dsi_display0=...`） |
| GOP | 1216×2688，32 bpp |
| splash | `splash_region 0xfc800000+0x2b00000`（43 MB） |
| 控制器 | `qcom,mdss_mdp@9800000`：mdp `0x9800000+0x1c8800`、vbif `0x9b16000+0x2008`、regdma `0x9b60000+0x7000`、ipcc `0x1100000+0x1000`、swfuse `0x9aa0000+0x13c`；IRQ 0x53；`qcom,sde-hw-version 0xd0000000` |
| DSI | ctrl0 `0x9ac0000+0x1000`（IRQ 4）、ctrl1 `0x9ac3000+0x1000`（IRQ 5）；phy0 `0x9ac1500`、phy1 `0x9ac4500`（v7.2，兼作 PLL 时钟源） |
| 面板供电 | `vddio` 1.8 V、`vdd` 1.25 V、`vci` 3.0 V（`dsi_panel_pwr_supply`） |
| TE / 复位 | TE GPIO 86（`&tlmm 0x56`）；面板复位 GPIO 98（`&tlmm 0x62`） |
| IOMMU | SDE unsec stream `0x800` mask `0x2`；sec stream `0x801` mask `0x0` |

节点清单、生成规则与验证见 `DISPLAY-DT.md`。

## 触控（TCM）

| 参数 | 值 |
|---|---|
| 总线 | SPI `spi@1a80000`（GENI，IRQ 0x358=856），`spi-max-frequency 0x2faf080`（50 MHz） |
| 节点 | `synaptics_tcm@0`，`synaptics,tcm-spi`，SPI 10 MHz |
| 复位 | GPIO 66（`&tlmm 0x42`），reset-delay 200 ms / active 20 ms |
| 中断 | GPIO 158（`&tlmm 0x9e`），flags `0x2008`（level-low + oneshot） |
| 电源 | `avdd`/`vdd`，power-delay 200 ms |
| 坐标 | ABS 0–12159 / 0–26879，10 指 |

## Wi-Fi / 蓝牙（WCN7880）

| 参数 | 值 |
|---|---|
| cnss | 实机 FDT 里**没有** `cnss-peach` 节点（该节点来自厂商 dtsi，已从 `nx741j-hw.dtsi` 移除）：WLAN 挂在 `pcie@1c00000/pcie_rp/cnss_pci0`；固件/MAC 等事实来自 vendor 文件，不是 DTB |
| PCIe | `pcie@1c00000`：parf/phy/dm_core/elbi/iatu/pcie_sm；BAR `0x40200000+0x100000`；IRQ 0x8c/0x95-0x98；`perst-gpio = &tlmm 102` |
| 域 | `linux,pci-domain 0`，bus-range 0–0xff，40 MSI 向量 |
| MAC | `5C:7D:AE:43:D9:F8`（persist `wifimac.dat`） |

## GPU

| 参数 | 值 |
|---|---|
| 节点 | `qcom,kgsl-3d0@3d00000`，compatible `qcom,adreno-gpu-gen8-2-1` |
| 型号 | `Adreno840v2`，`qcom,chipid 0x44050a01`（gen8.2.1） |
| 寄存器 | 3d00000+40000、rscc 3d50000+10000、cx_dbgc 3d61000+3000、cx_misc 3d9e000+2000 |
| 中断 | 0x12c（300）、0x50（80） |
| UBWC | mode 6，min access 0x20 |

## USB

| 参数 | 值 |
|---|---|
| 控制器 | `ssusb@a600000/dwc3@a600000`，`snps,dwc3`，reg `0xa600000+0xd93c` |
| 中断 | SPI 0x85（133） |
| SMMU | stream `0x40` |
| PHY | `hsphy@88e3000`、`ssphy@88e8000` |

## 音频

| 参数 | 值 |
|---|---|
| codec | `wsa884x`（`SpkrLeft`/`SpkrRight`，SoundWire `reg 0x2170220/0x2170221`，status disabled） |
| 供电 | `cdc-vdd-1p8` 1.8 V |
| 平台 | `canoe-mtp-wsa884x-snd-card`；`/vendor/etc/audio/sku_canoe/mixer_paths_canoe_mtp_wsa884x.xml` |

## 按键（TLMM）

| 键 | 引脚 |
|---|---|
| VOL+ | GPIO 101（active-low，pull-up） |
| FOCUS | GPIO 179 |
| SHOOT | GPIO 95 |
| VOL− | PMIC `pmk8350-pon / resin` |

## 与实机 DTB 的自动核对

`scripts/audit-live-dtb.py` 把本仓库声称描述的节点（`nx741j-hw.dtsi` 里的 10 个）逐个和实机 FDT 比对
`compatible`/`reg`/`interrupts`/`reg-names`/`status` 等属性：`reg` 按父节点的 cell 宽解码后比较（本仓库把节点
拍平到根，格数与嵌套的 stock 节点不同），`status` 把缺失/okay/ok 视作同一个值。

```sh
scripts/audit-live-dtb.py --live live-fdt.dtb --include <soC-dts>
```

2026-11-09 首次运行发现并修掉 12 处差异：`ufsphy_mem` 缺 `compatible`、`tlmm` 缺 `interrupts`、`kgsl` 的
`compatible` 少了 `qcom,kgsl-3d0`、`spi@1a80000` 的 `reg` 只有 2 格（父为 2/2 时应为 4 格，dtc 一直在告警）
且缺 `reg-names = "se_phys"`、七个节点的 `status` 与实机相反、`cnss-peach` 是不存在的节点。现在 10/10 一致。

## 编译验证

`dts/nx741j-hw.dtsi` 由 `dts/nx741j.dts` 引入；`./unixnu payload` 的 dtc 步骤会编译它。

## 保留内存（reserved-memory，实机 DTB 共 51 段、合计 836.6 MiB）

| 节点 | 基址 | 大小 | 属性 | compatible |
|---|---|---|---|---|
| `mpss_region@8aa00000` | `0x8aa00000` | 235.0 MiB | no-map | - |
| `lcp_dare_region` | `0x880000000` | 96.0 MiB | reusable | shared-dma-pool |
| `dsm_partition_1_region@84a00000` | `0x84a00000` | 85.0 MiB | no-map | - |
| `oem_vm_region@f7c00000` | `0xf7c00000` | 76.0 MiB | reusable | shared-dma-pool |
| `adspslpi_region@a1400000` | `0xa1400000` | 70.0 MiB | no-map | - |
| `trust_ui_vm_region@f3800000` | `0xf3800000` | 64.0 MiB | reusable | shared-dma-pool |
| `splash_region` | `0xfc800000` | 43.0 MiB | reusable | - |
| `qdss_apps_region@82800000` | `0x82800000` | 32.0 MiB | reusable | shared-dma-pool |
| `cdsp_region@9b500000` | `0x9b500000` | 25.0 MiB | no-map | - |
| `gunyah_hyp_region@80000000` | `0x80000000` | 14.0 MiB | no-map | - |
| `dsm_partition_2_region@89f00000` | `0x89f00000` | 10.5 MiB | no-map | - |
| `camera_region@99600000` | `0x99600000` | 8.0 MiB | no-map | - |
| `camera_2_region@99e00000` | `0x99e00000` | 8.0 MiB | no-map | - |
| `video_region@9a600000` | `0x9a600000` | 8.0 MiB | no-map | - |
| `tz_merged_region@d8000000` | `0xd8000000` | 8.0 MiB | no-map | - |
| `llcc_lpi_region@ff800000` | `0xff800000` | 8.0 MiB | no-map | - |
| `cvp_region@9ae00000` | `0x9ae00000` | 7.0 MiB | no-map | - |
| `soccp_region@a03d0000` | `0xa03d0000` | 5.0 MiB | no-map | - |
| `hypervisor_extended_heap_region@80e00000` | `0x80e00000` | 4.0 MiB | no-map | - |
| `cpucp_scandump_region@82000000` | `0x82000000` | 3.5 MiB | no-map | - |

其余 31 段为 <1 MiB 的日志/共享区。完整列表由 `stock/dt/live_fdt.dtb` 反编译得到。

## IOMMU（apps-smmu@15000000）

节点已进 `dts/nx741j-hw.dtsi`（`status = "ok"`，`qcom,qsmmu-v500`、`#iommu-cells = <2>`、`#global-interrupts = <1>`，
带 stock 的完整 `interrupts` 与 `qcom,actlr` 表）；显示片段的 `iommus` 通过 `&apps_smmu` 引用它。
ArmSmmu4K 组件是 kit、无归档，尚未被任何驱动消费。

### stream 分配

| 设备 | stream / mask |
|---|---|
| UFS | `0x60` |
| USB dwc3 | `0x40` |
| qcedev | `0xc0`/`0xc1`/`0xc3` |
| qpace | `0x2020`/`0x2024`/`0x2028`/`0x202c` |
| coresight TMC | `0x4e0` |
| GPU | 独立 `kgsl-smmu@3da0000`（`qcom,qsmmu-v500`） |

## 板级 ID

| 参数 | 值 |
|---|---|
| `qcom,board-id` | `<0x08 0x00>` |
| `qcom,msm-id` | `0x294/0x295` 及 `0x1000294/0x1000295`（soc_id 660/661） |
