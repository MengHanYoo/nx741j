# 步骤 2：共享支持选择记录

依据 <https://unixnu.com/zh/porting/> 步骤 2「选择共享支持」。
要求：读组件目录与候选实现，核对硬件版本/固件协议/OS ABI；按 SoC 支持步骤选已有实现或新建共享仓库；
核对资源、平台操作、组件依赖；**把源码版本固定到设备配置**；记录待完成的硬件或 ABI 工作。

## 1. 六个位置的选择结果

| 位置 | 选择 | 版本（精确） | 核对情况 |
|---|---|---|---|
| `UniXNU/profiles/arch/arm64` | 主仓库 arch profile | 主仓库 `11f5b5d4`（本地快照，dirty） | ✅ 含 4 KiB 页表、non-apple 平台、mockfs 等 53 条补丁 |
| `UniXNU-<soc>-common` | **自建 `UniXNU-sm8850-common`**（官方仅到 sm8550） | git `927698d5feca6d391e18ba9f7f2165d9d149735b`（5 文件，2026-11-06 提交） | ✅ 已提交；待推 remote |
| `UniXNU/profiles/os/<profile>` | `ios-16.7.16-20H392` | 主仓库同一提交 | ✅ xnu `8796.142.1.703.8`，Darwin 23.x |
| `UniXNU-<device>` | `UniXNU-nx741j` | git `8eee59444c381f946b7e2856a3864d86c5284db6`（20 文件） | ✅ 已提交；待推 remote |
| 组件仓库 | `ArmDTPlatform`、`ArmCpu`、`XnuPrelink`、`DarwinBoot` | `417ff95d`、`9066b53b`、`f876e3ee`、`3325e352` | ✅ 均在 `components.toml` 中带 revision |
| `uefi-<device>` | `/opt/work/uefi-sheng-main`（本地 git） | git `586670bcc53c7a05b7dcb9ecac494b209fd9525f`（2026-11-06 提交） | ✅ 已提交；待发布为 `uefi-nx741j` |

## 2. 为什么这样选

| 决策 | 理由 |
|---|---|
| SoC 层自建而不是复用 sm8550 | 官方无 SM8850 条目；sm8550 的 DTSI/GIC/时钟表与 Kaanapali 差异大（GIC 地址 0x17000000 一致但 RPMh/EPSS 布局不同） |
| OS profile 用 `ios-16.7.16-20H392` | 与已有 kernelcache（`/opt/work/ipsw/kernelcache.macho`）匹配，xnu 8796 系列；避免跨版本 ABI 风险 |
| 组件只选 4 个 | 其余组件（`GopFramebuffer`/`ShengUfs`/`Ath12kWifi`）依赖尚未完成的 XNU 侧适配，提前加入会导致内核链接失败 |
| `DarwinBoot` 用发布版 | 暂不需要 kext 注入；kext 方案需要 revision 2 加载器 |

## 3. 组件目录核对（硬件 / 固件协议 / OS ABI）

| 候选组件 | 硬件版本要求 | 本机 | 结论 |
|---|---|---|---|
| `ArmDTPlatform` | ARM64 通用 | SM8850 ✅ | 直接可用 |
| `ArmCpu` | GICv3 + generic timer | GICv3@0x17000000、19.2 MHz ✅ | 直接可用 |
| `XnuPrelink` | 工具，无硬件 | — | 直接可用 |
| `DarwinBoot` | ARM64 UEFI | ✅ | 直接可用 |
| `GopFramebuffer` | EFI GOP | SimpleFbDxe 1216×2688 ✅ | **计划加入**，等内核首次构建成功后 |
| `ShengUfs` | SM8550 UFS 控制器 + clocks | SM8850 UFS（KLUGG8NHKB）⚠️ | 需改 SM8850 时钟表后再选 |
| `Ath12kWifi` | WCN7850 PCIe | **WCN7850 (17cb:110e)** ✅ | 硬件匹配，等 PCIe host 支持 |
| `QcomGeniSpi` | GENI SPI | SPI 触控 ✅ | 与触控组件一起选 |
| `ShengNovatekTouch` | Novatek NT36532 | **Synaptics TCM** ❌ | 不适用，需新建协议层 |
| `Adreno740` | Adreno 740 / SM8550 | Adreno 840 / gen80200 ❌ | 需移植 |
| `Sm8550Platform` | SM8550 时钟/SMMU | SM8850 ⚠️ | 需新建 SM8850 版 |

## 4. 待完成的硬件 / ABI 工作

| # | 工作 | 类型 | 阻塞对象 | 状态 |
|---|---|---|---|---|
| 1 | 4 KiB 页路径验证（`ARM_PGSHIFT=12`、4K user ABI、4K 签名哈希） | ABI | 内核首次运行 | 补丁已在 arch 序列，未验证 |
| 2 | 预链接 ABI：`[kernel.prelink]` 字段 + kernelcache 哈希 | ABI | `kernel.macho` | 未开始（工具链已就绪） |
| 3 | ADT 平台种子（`unique_chip_id`） | 硬件 | payload/ADT | 现为占位值，需真机读 |
| 4 | SM8850 平台操作（时钟/RPMh/SMMU） | 硬件 | UFS/显示/电源的 XNU 驱动 | 未开始 |
| 5 | Synaptics TCM 触控协议层 | 硬件 | 触控 | 未开始（`ShengNovatekTouch` 不适用） |
| 6 | Adreno 840 移植（gen80200 固件、GMEM 18 MiB） | 硬件 | GPU | 内核侧曾验证 |
| 7 | kext 加载器 revision 2 | ABI | kext 方案 | 暂不需要 |
| 8 | 第 2 层 DXE 链（`RpmhDxe`） | 硬件 | 整条启动链 | 卡住，建议换根 |

## 5. 版本固定方式（当前与目标）

| 对象 | 当前 | 目标 |
|---|---|---|
| 主仓库 | git `11f5b5d4`（dirty 本地快照） | 收敛到上游 `e2fdae4` + 本地分支 |
| `UniXNU-sm8850-common` | 目录哈希 `05ef8153…` | git 仓库 + 设备 `feeds.toml` 钉 commit |
| `UniXNU-nx741j` | 目录哈希 `69a9925d…` | git 仓库 + 主仓库 `devices.toml` 钉 commit |
| `uefi-sheng-main` | git `b62f2a95`（dirty） | 发布为 `uefi-nx741j` |
| 组件 | `components.toml` revision | 不变（已固定） |

**结论**：六个位置都已选定，四个官方组件版本已固定；两个自建仓库和固件仓库**尚未发布为 git 仓库**，
因此"精确版本"目前以目录哈希记录，发布后再切换为 commit —— 这是步骤 2 唯一的形式缺口，不影响继续做步骤 3。

## 6. 版本锁（unixnu.lock，2026-11-06 生成）

`./unixnu fetch` 已生成 `unixnu.lock`（694 B），固定 8 个 git 源：

| 源 | commit |
|---|---|
| xnu | `1b191cb58250d0705d8a51287127505aa4bc0789` |
| edk2 | `cfb93724a06ff86ff5ba971413368a7809ee42a5` |
| AvailabilityVersions | `933f9360f1b70dfbda5548386a3d65e0358b4f7d` |
| IOStorageFamily | `e45fbe30a7879e0b16e26dddb78538f1b00e0bff` |
| corecrypto-2023 | `11fba65b12d8830630e248dfa8344caf8b065f26` |
| libdispatch | `30fe6a7d945340ce69cef121132f049e810089a5` |
| libplatform | `a00a4cc36da2110578bcf3b8eeeeb93dcc7f4e11` |
| libpthread | `c0e60fb5ecfc54cf379c261d3f9572cc22ded891` |

**限制**：lock 文件头明确写着 *"Local sources are never recorded"*——`UniXNU-sm8850-common` 走 `local` 源，
因此**不会被锁**。这是"自建 SoC 仓库未发布"的直接后果，也是步骤 2 唯一未闭环的形式项。
