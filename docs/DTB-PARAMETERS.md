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
| cnss | `cnss-peach@b0000000+0x10000`，`qcom,wlan-rc-num 0`，`wlan-sw-ctrl-gpio = &tlmm 19` |
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

## 编译验证

`dts/nx741j-hw.dtsi` 由 `dts/nx741j.dts` 引入；`./unixnu payload` 的 dtc 步骤会编译它。
