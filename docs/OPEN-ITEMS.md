# 未完成项（按官方页面归类）

更新于设备仓库 revision `4b410c2513b7db0fe14fc6420516659a7b6c0c12`。状态：`完成` / `进行中` / `阻塞`（缺硬件或信息）。

| # | 项 | 官方页 | 状态 | 下一步 |
|---|---|---|---|---|
| 1 | 固件集成可复现 | [UEBI 固件](/zh/porting/uefi-firmware) | 完成（本次） | `package/firmware-overlay/` 已收录 DSC/FDF/incs/驱动；构建前运行 `scripts/apply-firmware-overlay.py <uefi-tree>` |
| 2 | 菜单 → DarwinBoot 交接 | [启动链](/zh/concepts/boot-chain)、[Payload](/zh/concepts/payload) | 阻塞 | 菜单倒计时结束后直接 MemoryDump，`DARWINBOOT: Boot called` 未出现；下一步在 `Menu.c` 超时返回与 `UniXNUBootEntry` 入口加 `DEBUG` 并逐行确认 |
| 3 | UEFI 按键 | [触控与按键](/zh/porting/input) | 阻塞 | TLMM 读探测：GCD `type 3 len 0x500000`，`0x0F000000`/`0x0F00FFF0` 可读，`0x0F010000` 起读崩；需 DxeCore/ArmMmuLib 建映射或改 PMIC/SPMI |
| 4 | 存储（UFS） | [UFS 存储](/zh/porting/ufs) | 未开始 | 目录 `ShengUfs` 标注 SM8550 控制器；先核对 SM8850 UFS 寄存器差异再选组件 |
| 5 | 显示/亮度 | [显示](/zh/porting/display) | 未开始 | `GopFramebuffer` 已选；内核侧显示与亮度驱动待适配 |
| 6 | 触摸 | [触控与按键](/zh/porting/input) | 未开始 | 确认面板是否 Novatek NT36532；`QcomGeniSpi` + `ShengNovatekTouch` 需板级接入 |
| 7 | GPU/Metal | [GPU 与 Metal](/zh/porting/gpu) | 未开始 | 目录 `Adreno740`/`AdrenoMetal` 面向 SM8550/A740，SM8850 需对应实现 |
| 8 | Wi-Fi/蓝牙 | [Wi-Fi](/zh/porting/network) | 未开始 | 确认模组型号（目录参考 WCN7850） |
| 9 | USB/基带/音频 | [USB](/zh/porting/usb)、[基带](/zh/porting/baseband)、[电源](/zh/porting/power) | 未开始 | 按驱动顺序在存储/显示之后 |
| 10 | 专有文件哈希 | [设备仓库](/zh/porting/device-repository) | 阻塞 | `.cache/firmware` 为空；导入 stock 文件后回填 `proprietary-files.txt` |
| 11 | userdata 双系统分区 | [双系统分区](/zh/porting/dual-system-partition) | 未开始 | `board.toml [userdata]` 的 `first_lba/last_lba` 未填（需 GPT 实测） |
| 12 | 记录完成等级 | [适配记录](/zh/porting/records) | 进行中 | 仅 `firmware` 为 device-verified；`storage/input` 为 planned，其余按证据升级 |
| 13 | 打包脚本 | [设备打包脚本](/zh/porting/packaging) | 进行中 | `scripts/package.sh` 需在干净检出跑通并记录产物 |
| 14 | 发布 | [设备仓库 §发布](/zh/porting/device-repository#publish) | 阻塞 | 需要 `unixnu` topic、`Porting record` CI 通过、设备页收录 |

## 本次已完成的配置收口

- 固件 overlay 收录全部板级改动（DSC/FDF/APRIORI/DXE/驱动/SEC/KeyCallback/RfsProtection）；
- README 状态与 `dts/nx741j.dts` 路径修正；
- 启动配置、组件选择、本地开发、步骤 1–4 记录均在 `docs/` 下。
