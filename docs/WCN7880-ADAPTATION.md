# WCN7880 适配差异清单

对照 `UniXNU/Ath12kWifi`（面向 WCN7850）与 `UniXNU/QcaBluetooth`，列出 nx741j 实机
（WCN7880）需要改动的板级输入。设备仓库 revision `b4d508433ec44b5980b3ea5f4973abe5289fe63c`。

## 实机事实

| 项 | 值 | 来源 |
|---|---|---|
| 芯片 | **WCN7880**（`phyAni*_wcn7880`，BT ROM 自报 `WCN7880ROM=0200`） | `modem_a` 字符串 |
| PCI 位置 | `0000:01:00.0` | `/sys/class/net/wlan0/device` |
| BT 固件 | `BTFW.BRAHMA.2.0.1-00181-PATCHZ-1`、`BTFW.GANGES.2.0.1-00184-PATCHZ-1`（`BRHBTFW2.MBN`/`GNGBTFW2.MBN`，291,532 B） | `bluetooth_a` FAT |
| WLAN board data | `/IMAGE/PEACH/BDWLAN.E01…E49`（72×125,420 B）+ `BDWLAN.ELF` | `modem_a` FAT |
| WLAN 配置 | `/vendor/etc/wifi/peach_v2/WCNSS_qcom_cfg.ini` | vendor EROFS |
| MAC | `5C:7D:AE:43:D9:F8`（`persist/wifimac.dat`） | persist dump |
| WLAN 固件 | `wlanmdsp.mbn` 为运行时生成（`/vendor/firmware/wlanmdsp.otaupdate` 符号链接） | vendor EROFS |
| PCIe | `pcie@1c00000`，SM 寄存器 `0x1D07000`（bootargs `pci-msm-drv.pcie_sm_regs`） | stock DTB bootargs |
| 供电/GPIO | `cnss_wlan_en` GPIO 16；`cnss_wlan_region` 32 MB DMA 池 | stock DTB |

## 与组件实现的差异

`Ath12kWifi` 已把硬件细节抽象为板级回调，适配点集中在 `nw_board`：

| 字段/接口 | 组件期望 | nx741j 需提供 | 状态 |
|---|---|---|---|
| `device_id` | PCI device id（WCN7850 为 `0x110e`） | **WCN7880 的 device id 待读 PCI config**（Android SELinux 拒绝 `/sys/bus/pci/devices/.../device`） | ⚠️ 待测 |
| `bar` / `bar_len` | BAR0 基址/长度 | PCIe 枚举后由 host 提供 | ⚠️ 依赖 PCIe host |
| `irq_base` / `msi_cap` | MSI 向量基号 | PCIe host + MSI 控制器 | ⚠️ 依赖 PCIe host |
| `cfg_read` / `cfg_write` | PCI 配置空间访问 | 板级 ECAM/PCIe 实现 | ⚠️ |
| firmware 加载 | Linux 风格 `request_firmware` | 映射到 `modem_a /IMAGE/PEACH/BDWLAN.*` 与 `wlanmdsp.mbn` | ⚠️ 文件名/打包方式待定 |
| `QcaBluetooth` UART | `uart_write/drained/speed/flow` | GENI UART（qup）基址/IRQ 需从 DTB 取 | ⚠️ |
| BT 固件命名 | `firmware_get(name,…)` | `bluetooth_a` 中 BRAHMA/GANGES 两套，需按芯片 ROM 选择 | ⚠️ |
| BT NVM | `.B*` 后缀文件（`BRHBTNV2.B34` 等） | 与固件版本匹配的 NVM 文件 | ⚠️ |

## 适配步骤（建议）

1. 从 PCIe 枚举读 `device_id`（可用内核 PCIe host 或 9008 之外的 UEFI PCI 枚举）→ 填板级 `nw_board.device_id`；
2. 在 `board.toml [components] include` 选择 `Ath12kWifi`/`QcaBluetooth`，按官方 archives 规则加入；
3. 实现板级 `firmware_get`：把 `BDWLAN.E**`、`WCNSS_qcom_cfg.ini`、`wlanmdsp.mbn` 打包进
   payload 或从分区读取；
4. 蓝牙侧按 `device_id`/ROM 选择 BRAHMA 或 GANGES 的 `.MBN`+`.TLV`+`.B*` NVM；
5. 用 `[userdata]` 的 MAC 或 `wifimac.dat` 填充 `wlan_mac.bin`。

## 存疑

| 项 | 说明 |
|---|---|
| PCI device id | 当前无法从 Android shell 读；需内核/PCIe host 或 UEFI PCI 枚举 |
| GENI UART 基址/IRQ | 需在 stock DTB 找 `qcom,qup_uart`/`bluetooth` 节点并记录 |
| `wlanmdsp.mbn` 来源 | 运行时生成，需从 `modem` 或 vendor 提取其源镜像 |
| 组件许可 | 两个组件按 WCN7850 验证，WCN7880 的 FW/NVM 组合未验证 |
