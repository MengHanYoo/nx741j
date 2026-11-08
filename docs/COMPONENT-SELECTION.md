# 组件选择（nx741j / SM8850）

对应官方[组件目录](https://unixnu.com/zh/reference/components)：设备 `[components] include`
替换继承数组，解析器补全依赖，按 `build_stage` 顺序构建；`./unixnu status --json -D …`
给出最终组件、源码版本与顺序。

## 当前选中

`board.toml`：

```toml
[components]
include = ["ArmDTPlatform", "ArmCpu", "SoftwareAES", "XnuPrelink", "DarwinBoot", "GopFramebuffer"]
```

| 组件 | 选择理由 |
|---|---|
| `ArmDTPlatform` | 阶段 3 需要 DT 平台专家（`CONFIG_XNU_ARM_DT_PLATFORM`） |
| `ArmCpu` | GICv3 + 通用定时器（`CONFIG_XNU_ARMCPU`） |
| `SoftwareAES` | arch profile 的 `[kernel].archives` 引用 `component:SoftwareAES` |
| `XnuPrelink` | 预链接阶段工具 |
| `DarwinBoot` | 固件内 XNU loader；阶段 3/4 交接 |
| `GopFramebuffer` | 嵌入式显示；payload 帧缓冲交给 XNU |

## 目录逐项核对（28 个）

| 组件 | 类型 | 阶段 | 硬件/ABI | 决定 | 说明 |
|---|---|---|---|---|---|
| `ArmCpu` | driver | kernel | ARM GICv3, ARM generic timer | 选中 | 与当前阶段（DT/CPU/AES/预链接/固件/显示）匹配 |
| `ArmDTPlatform` | driver | kernel | ARM64 | 选中 | 与当前阶段（DT/CPU/AES/预链接/固件/显示）匹配 |
| `ArmVirtPlatform` | driver | kernel | QEMU virt ARM64 | 不适用 | QEMU/虚拟化或其它设备专用 |
| `DarwinBoot` | driver | firmware | ARM64 UEFI | 选中 | 与当前阶段（DT/CPU/AES/预链接/固件/显示）匹配 |
| `Adreno740` | driver | kernel | Adreno 740, SM8550 | 计划 | GPU 内核接口；目录标 A740/SM8550，SM8850 需对应实现 |
| `AdrenoMetal` | driver | payload | Turnip Darwin backend, kernel GPU user-client ABI | 计划 | Metal 前端；依赖 GPU 内核接口与 OS ABI |
| `ShengUfs` | driver | kernel | SM8550 UFS controller | 计划 | UFS 存储；目录标 SM8550 控制器，需先核对 SM8850 UFS 差异 |
| `ShengHidInput` | driver | kernel | sheng touch event source | 计划 | 输入事件源；需与板级触摸/按键驱动配套 |
| `ShengNovatekTouch` | driver | embedded | Novatek NT36532/NT36532E | 计划 | 触摸；待确认 nx741j 面板是否 Novatek NT36532 |
| `QcomGeniSpi` | driver | embedded | Qualcomm GENI SPI | 计划 | 触摸 SPI；GENI SPI 在 SM8850 上可用，待接入 |
| `GopFramebuffer` | driver | embedded | UEFI GOP framebuffer | 选中 | 与当前阶段（DT/CPU/AES/预链接/固件/显示）匹配 |
| `SoftwareAES` | driver | kernel | XNU AES routines | 选中 | 与当前阶段（DT/CPU/AES/预链接/固件/显示）匹配 |
| `AppleKeyStoreState` | driver | kernel | AppleKeyStore ABI | 计划 | AppleKeyStore ABI；用户态/安全阶段再启用 |
| `RamBlock` | driver | kernel | RAM block backing | 不适用 | QEMU/虚拟化或其它设备专用 |
| `VirtioMmioBlock` | driver | kernel | VirtIO 1.2 modern MMIO split queues | 不适用 | QEMU/虚拟化或其它设备专用 |
| `VirtioMmioConsole` | driver | kernel | VirtIO 1.2 modern MMIO split queues | 不适用 | QEMU/虚拟化或其它设备专用 |
| `XnuPrelink` | kit | prelink | — | 选中 | 与当前阶段（DT/CPU/AES/预链接/固件/显示）匹配 |
| `AdrenoPM4` | kit | embedded | Adreno packet type 4 and 7, A740 command layouts | 计划 | 嵌入式 kit，随对应驱动阶段引入 |
| `AdrenoFirmware` | kit | embedded | Adreno A6xx/A7xx HFI | 计划 | 嵌入式 kit，随对应驱动阶段引入 |
| `ArmSmmu4K` | kit | embedded | ARM SMMU, MMU-500 | 计划 | 嵌入式 kit，随对应驱动阶段引入 |
| `AirNir` | kit | embedded | AIR and NIR interfaces | 计划 | 嵌入式 kit，随对应驱动阶段引入 |
| `VerifiedRoot` | kit | embedded | — | 计划 | 嵌入式 kit，随对应驱动阶段引入 |
| `Sm8550Platform` | kit | embedded | Qualcomm SM8550 | 计划 | 嵌入式 kit，随对应驱动阶段引入 |
| `Ath12kWifi` | driver | embedded | ath12k PCI devices, qcc2072, qcn9274, and wcn7850 build inputs | 计划 | Wi-Fi；目录标 WCN7850，需确认 nx741j 模组 |
| `QcaBluetooth` | driver | embedded | Qualcomm WCN7850 UART controller, Qualcomm GENI UART | 计划 | 蓝牙；同上 |
| `QcomDwc3` | driver | embedded | Qualcomm DWC3 USB controller, Synopsys eUSB2 PHY and PM8550B repeater, QMP USB3/DP PHY, PS5169 retimer | 计划 | USB；DWC3 控制器需按 SM8850 核对 |
| `ShengPreflight` | driver | firmware | Xiaomi Pad 6S Pro | 不适用 | QEMU/虚拟化或其它设备专用 |
| `QcomBaseband` | driver | kernel | Qualcomm QMI over QRTR, SC7180 IPA v4.2 reference | 计划 | 基带；目录为 SC7180 IPA 参考实现，SM8850 需替换 |

## 本地源码覆盖

新组件或本地修改按官方方式显式指定（不写入目录）：

```sh
./unixnu tree -D ../UniXNU-nx741j --local-module driver:MyDriver=/path/to/MyDriver
```

已选组件在 `local.toml` 中按 `[[source]]` 覆盖（本仓库当前只覆盖 SoC）：

```toml
[[source]]
kind = "driver"
name = "GopFramebuffer"
local = "../GopFramebuffer"
```

## 证据

- `./unixnu status --json -D ../UniXNU-nx741j`：组件 6 个、module 20 个、补丁 63 条；
- `python3 scripts/porting-record.py check --device …`：14 个结构化区域通过；
- 设备仓库 revision `b8e750a5f422571874bffa7395af6db52b719bb4`。
