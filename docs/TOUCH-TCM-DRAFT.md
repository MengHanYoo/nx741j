# Synaptics TCM 触摸驱动接口草案

依据实机 DTB 的 `synaptics_tcm@0` 节点与官方 drivers 页的接口五要素。设备仓库 revision `b4d508433ec44b5980b3ea5f4973abe5289fe63c`。

## 硬件（实机 DTB）

| 项 | 值 |
|---|---|
| compatible | `synaptics,tcm-spi` |
| 总线 | SPI（GENI SPI），`reg = <0>`，`spi-max-frequency = 10000000` |
| 中断 | GPIO 158，`interrupt-parent` = TLMM，`interrupts = <0x9e 0x2008>`（level-low + oneshot） |
| 复位 | GPIO 66（`synaptics,reset-gpio = <0x33 0x42 0x00>`），`reset-delay-ms = 200`、`reset-active-ms = 20` |
| 电源 | `avdd`/`vdd` supply，`power-delay-ms = 200` |
| 面板绑定 | `panel = <0x7e8>`（bf375/rm692h5） |
| 坐标 | ABS_X 0–12159、ABS_Y 0–26879（10 指，`ABS_MT_SLOT` 0–9） |
| 固件 | `/vendor/firmware/syna_firmware.img`（241,578 B，sha256 `f6a68725…`，`BOOT_CONFIG` + `#SYNA CFG$]`） |

## 传输协议（TCM）

1. 上电/复位序列：拉低 reset 20 ms → 释放 → 等待 200 ms；
2. 读取 `BOOT_CONFIG` 头，确认 firmware/配置版本；
3. 通过 SPI 发送 TCM 报告读取命令（0x?? 待按 `synaptics_tcm` 协议确认），读取 touch report；
4. 解析 report → 触摸点（slot/tracking id/x/y/pressure）；
5. 中断：GPIO 158 下降沿/低电平触发，oneshot。

## 内存

- SPI 传输缓冲小（≤ 4 KiB），无 DMA 窗口；
- 报告缓冲由 IOHIDDevice 提供。

## OS 接口

- 复用 `ShengHidInput` 的 `IOHIDDevice` 通路发布触摸事件；
- 新组件建议命名 `SynapticsTcm`（板级 package 或组件仓库），provider 匹配 `synaptics,tcm-spi`；
- 与 `QcomGeniSpi` 的 SPI 传输接口对接（GENI SE base/IRQ 由 DTB 提供）。

## 构建接入

```toml
[components]
include = ["SynapticsTcm", "QcomGeniSpi", "ShengHidInput"]

[kernel]
archives = [..., "component:SynapticsTcm", "build-virtio-mmio-bindings.py ..."]
```

## 验证步骤（官方 drivers 第 8 条）

1. SPI 读 chip ID/版本（读到 `BOOT_CONFIG` 即认为总线通）；
2. 解析一次 touch report（按下/抬起）；
3. IOHIDDevice 发布事件，用户态可见触点；
4. 多点（10 指）与坐标边界（12159/26879）核对。

## 存疑

| 项 | 说明 |
|---|---|
| TCM 命令码 | 需对照 Linux `synaptics_tcm` 驱动确认读报告命令与长度 |
| 中断触发方式 | DTB 为 level-low + oneshot，需确认 TCM 固件是否电平保持 |
| 固件加载路径 | `/vendor/firmware` 在 payload 中的映射方式待定 |
| 组件归属 | 新建组件还是放设备 `package/`，需按官方 drivers 页选择 |
