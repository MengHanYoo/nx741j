# 工具箱免解 BL Root 路线 vs UniXNU efisp 路线

输入：用户提供的《骁龙8 Gen5 锁BL 持久化Root 完整技术教程.doc》（OPPO / SM8850 "8e5" /
Android 16，2026-02）。设备仓库 revision `49de2eae9e21688fa572e7a818b9de69f43d7521`。

## 1. 该教程实际做了什么（据文档内容）

| 步骤 | 内容 |
|---|---|
| 环境 | Ubuntu 25.10；UEFI `uefi.lnx.5.0.r49-rel`（SM8850，`CANOE=1` target）；UEFITool、IDA、fastboot、Magisk |
| 备份 | `dd` 备份 `efisp`、`abl`、`boot`、`init_boot` |
| 改 UEFI | 修改/重编 OPPO 的 `abl.efi`、`fastboot.efi`（`oplus` 工程），使 fastboot 可用 |
| 改 efisp | 把 `loader.efi` patch 后写入 `efisp` |
| 结果 | BL 仍 `locked`，但 `fastboot getvar unlocked` 可用；刷 Magisk patched `init_boot.img` 获得 Root，Widevine L1 保留 |

关键点：它**不是**把第三方 UEFI 装进 `boot`，而是**修改厂商 UEFI/ABL 的 efisp 链路**，
让锁定态仍能执行 patched fastboot/loader，再靠 Magisk 改 `init_boot` 拿 Root。

## 2. 与本机 UniXNU efisp 路线的关系

| 项 | 工具箱教程 | 本机 UniXNU |
|---|---|---|
| 载体 | `efisp` 分区（3 MiB PE） | **相同**：LUN4 LBA 748894，3 MiB PE |
| efisp 内容 | patch 过的厂商 `loader.efi` | 自研 P5/P6 loader（读 rawdump → FD → 0xFF300000） |
| UEFI 来源 | 厂商 UEFI（patch 后） | Mu-Silicium 自编 FD（4 MiB，来自 rawdump） |
| 目的 | fastboot + Magisk Root | DarwinBoot + XNU（UniXNU 菜单） |
| Secure Boot | 通过 patch efisp/ABL 规避 | 同：efisp 不在 AVB 链，天然绕过 |
| 依赖 | 厂商 ABL 的 efisp 加载行为 | 同，且本机已实测 P5 loader 能起 FD |

结论：教程证明"锁定 SM8850 上 efisp 阶段可执行任意 PE"这件事在本机同样成立；这正是我们
P5 loader 已经验证的路径。它**不涉及** UniXNU 的 Mu-Silicium 固件、ADT、DarwinBoot。

## 3. 回答：能否按官方文档引导 UEFI 并启动到 UniXNU 菜单？

| 阶段 | 状态 | 说明 |
|---|---|---|
| efisp → 加载自编 UEFI FD | ✅ 已实现 | P5/P6 loader：rawdump payload/FD → `0xFF300000` |
| UEFI 启动（DXE/BDS） | ✅ 已实现 | 屏幕出现完整 DXE 日志、`UniXNU` 菜单、倒计时 |
| **UniXNU 菜单** | ✅ **已到达** | 菜单显示 NX741J，能倒计时 |
| 菜单 → DarwinBoot | ❌ 阻塞 | 倒计时后 MemoryDump，未打印 `DARWINBOOT: Boot called`（层 4） |
| 固件稳定性 | ⚠️ 分支差异 | 某些 FV 在 `DALSYS.efi` 后崩溃；V65+ 能进 BDS |

所以：**"efisp 加载 UEFI 并到 UniXNU 菜单"在官方文档与本机实践上都已经成立**，教程只是
旁证了 efisp 阶段的可执行性；真正没解决的是**菜单之后的交接（层 4）和固件稳定性**，
这两点与工具箱的 fastboot/Magisk 路线无关。

## 4. 可借鉴 / 不可照搬

| 可借鉴 | 原因 |
|---|---|
| 先备份 `efisp`/`abl`/`boot`/`init_boot` | 与我们的 stock efisp 恢复流程一致，值得固定成脚本 |
| patched `fastboot.efi` 思路 | 若将来需要锁定态免 9008 刷写，可研究 ZTE UEFI 的 fastboot 入口（设备特定） |
| UEFI 内读分区/写文件 | 与我们的 loader 读 rawdump 同类，可扩展为 UEFI shell 校验 |

| 不可照搬 | 原因 |
|---|---|
| 刷 OPPO 的 `abl.efi`/`fastboot.efi` | 厂商/签名/内存布局不同，必砖 |
| 用厂商 UEFI 直接跑 UniXNU | 厂商 UEFI 是 ACPI/Linux 导向，缺 DarwinBoot、ADT、IOKit 平台契约 |
| 依赖 Magisk 改 `init_boot` | 与 XNU 无关；我们的目标是原生 Darwin |

## 5. 下一步（按官方文档）

1. **层 4 探针**：刷 V67（`fd-v67-layer4.img`）定位菜单返回后断点（当前 V67 暂缓）；
2. 若卡在 `DALSYS.efi`：用更小/更完整 FV 二分（历史 V43–V65 已有）；
3. 检查 ADT/DT 与 `ArmDTPlatform` 的 `/chosen` NVRAM proxy（阶段 5 前置）；
4. efisp 侧用 P6（fail-closed）替换 P5，减少坏镜像导致的死机窗口。
