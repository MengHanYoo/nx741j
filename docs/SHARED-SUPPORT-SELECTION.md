# 共享支持选择与版本固定

对应 `reference/` 第 2 项"选择共享支持"。下表是**已选仓库及其精确版本**，以及仍需完成的硬件/ABI 工作。

## 1. 已选仓库与精确版本

| 角色 | 仓库（本地路径） | 版本（commit） | 固定方式 |
|---|---|---|---|
| 框架 + arch/os profile | `repos/UniXNU` | 工具本体（非源码输入）；其解析结果记录在 `unixnu.lock` | `unixnu.lock` 固定 xnu `1b191cb5`、edk2 `cfb93724`、libpthread `cf9e1c7e`、libplatform `a00a4cc3`、libdispatch `30fe6a7d`、corecrypto `11fba65b`、AvailabilityVersions `933f9360`、IOStorageFamily `e45fbe30` |
| SoC 共享支持 | `repos/UniXNU-sm8850-common` | `825a5304e2b72b55ac67501f56d12cc905f2f458` | 设备仓库 `feeds.toml` 的 `[[source]] name="sm8850"` 精确 pin ✅ |
| 设备/板级 | `repos/UniXNU-nx741j` | `300fa8e3ef76` | 设备仓库自身；提交即版本 |
| 固件平台 | `/opt/work/uefi-sheng-main`（发布仓 `MengHanYoo/uefi-nx741j`） | `cfad0458567c` | `unixnu.local.toml [build] uefi = …` 为本地构建输入（官方不把 UEFI 树写入 feeds/lock）；commit 记录在此供复现 |

> 说明：`unixnu.lock` 已提交，`unixnu status --json` 报告所有源码均可复现；框架工具本身不是源码输入，
> 设备仓库以提交号固定，UEFI 树以本地路径 + 此处记录的 commit 固定。

## 2. 已选组件（来自 `UniXNU/components.toml`，均带 revision）

`board.toml [components] include` 选中 6 个：`ArmDTPlatform`、`ArmCpu`、`SoftwareAES`、
`XnuPrelink`、`DarwinBoot`、`GopFramebuffer`（解析器不再补依赖）。

| 组件 | 仓库 | revision |
|---|---|---|
| `ArmCpu` | `UniXNU/ArmCpu` | `93a5030c5d98` |
| `ArmDTPlatform` | `UniXNU/ArmDTPlatform` | `417ff95d6a6e` |
| `SoftwareAES` | `UniXNU/SoftwareAES` | `4a45326da5c2` |
| `XnuPrelink` | `UniXNU/XnuPrelink` | `c645d76bb0ab` |
| `DarwinBoot` | `UniXNU/DarwinBoot` | `3c75734014cd` |
| `GopFramebuffer` | `UniXNU/GopFramebuffer` | `e47c1b8de169` |

候选实现核对（硬件版本 / 固件协议 / OS ABI）：

| 组件 | 硬件/协议核对 | 结论 |
|---|---|---|
| `ArmCpu` | GICv3 @0x17000000 + generic timer 19.2 MHz，与本机 DT 一致 | 直接复用 ✅ |
| `ArmDTPlatform` | 依赖板级 DTS（`dts/board.dts` + `soc:dts`） | 直接复用 ✅ |
| `GopFramebuffer` | 面板 bf375/rm692h5，UEFI GOP 1216×2688 | 直接复用 ✅ |
| `SoftwareAES` | 与 `CONFIG_XNU_NATIVE_AES` 互斥选择 | 直接复用 ✅ |
| `XnuPrelink` | 需原始 kernelcache（`/opt/work/ipsw/kernelcache.arm64`） | 已产出 `kernel.macho`，但 `runtime_qualified=false` ⚠️ |
| `DarwinBoot` | XNU 引导链 | 已接入 ⚠️ 见第 3 节 |
| `ShengUfs` / `ShengNovatekTouch` / `Adreno740` 等 | SM8550/sheng 平台专用 | **不选**（不匹配 SM8850/NX741J） |

## 3. OS profile 与 ABI 决策

- 选定 **`profiles/os/ios-16.7.16-20H392`**（XNU 8796.141.3，arm64，4 KiB 页）；
- `profiles/arch/arm64` 通用构建配置 + 补丁（63 条 series）；
- **不使用** `ipados-17.7.11-21H461`（原有 sheng 平台 profile）；
- **arm64e / iOS 27 不适用**：本机目标是 arm64 的 iOS 16.7.16；reference 明确 arm64e 仅限受支持设备。

待完成的 ABI 工作（阻塞硬件驱动验证）：

| # | 项目 | 现状 |
|---|---|---|
| A1 | prelink ABI 资格 | `runtime_qualified = false`；`early_security_init` 为空 → AMFI 早期初始化不执行 |
| A2 | payload ADT | `chip_id = "207"`、`unique_chip_id = "required"` 已配置，未在设备上验证 |
| A3 | 内核缓存匹配 | `roots = [AppleARMPlatform, apfs]`，匹配器按依赖补全；未做全量组件树比对 |

## 4. 固定项状态（2026-11-08 复核）

1. **`unixnu.lock` 已提交** ✅ → xnu/edk2/sm8850/libpthread/libplatform/libdispatch/corecrypto/AvailabilityVersions/IOStorageFamily 全部 pin；
2. **UEFI 固件树**：官方未要求写入 feeds/lock，按"本地构建输入"处理；当前 commit `cfad0458567c` 记录在第 1 节，`unixnu.local.toml` 只给路径；
3. 组件 revision 来自 `components.toml`（离线可信），上游远端**尚未逐个核对**（需联网，本轮未做）。

## 5. 待完成的硬件工作（与第 1 项缺失清单对应）

| # | 项目 | 阻塞点 |
|---|---|---|
| H1 | UFS → BlockIo | **已绕过**：payload 卷由 `UniXNURamDiskDxe` + x0 FDT 发布（`initrd block device … Success`、`Installed Fat filesystem`）；UFS 仅根卷阶段需要 |
| H2 | `DTBExtnProtocol` 提供者 | **确认不存在**：`Binaries/nx741j` 180 个驱动只有使用方、无安装方 |
| H3 | SMEM 读到 0 | 🟡 **协议可用**：V39c 中 `DALSys.efi` 成功启动（`SmemStub: item 404 -> …`）；数据内容未核 |
| H4 | XNU 驱动 | 0/9（UFS、显示、触控、Wi-Fi、USB、音频、GPU…） |

## 6. 步骤 2 审计结论（2026-11-08）

| 官方要求 | 状态 | 依据 |
|---|---|---|
| 选择已有实现或建立共享仓库 | ✅ | SoC 复用 `UniXNU-sm8850-common`（`825a5304`）；组件 6 个全部复用现有实现 |
| 核对硬件版本 / 固件协议 / OS ABI | ✅ | 第 2 节逐组件核对表；不匹配的 SM8550 专用组件明确不选 |
| 在设备配置中固定源码版本 | ✅ | `feeds.toml` pin SoC；`unixnu.lock` pin 全部 git 源码；设备仓库以提交固定 |
| OS profile 与 ABI 待办已记录 | ✅ | A1–A3（prelink 资格、ADT 设备验证、组件树比对） |
| 硬件待办已记录 | ✅ | H1–H4 更新为当前状态 |

**结论**：步骤 2 的交付物（已选仓库 + 精确版本 + 待完成硬件/ABI 工作）已具备；剩余是 A1/A3 的 ABI 验证与 H4 的驱动实现，属后续阶段。
