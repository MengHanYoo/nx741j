# 共享支持选择与版本固定

对应 `reference/` 第 2 项"选择共享支持"。下表是**已选仓库及其精确版本**，以及仍需完成的硬件/ABI 工作。

## 1. 已选仓库与精确版本

| 角色 | 仓库（本地路径） | 版本（commit） | 固定方式 |
|---|---|---|---|
| 框架 + arch/os profile | `repos/UniXNU` | `1e612bdd359b` | 设备仓库 `board.toml [package] unixnu = "../UniXNU"`；**未写 commit pin（缺 lock）** |
| SoC 共享支持 | `repos/UniXNU-sm8850-common` | `825a5304e2b72b55ac67501f56d12cc905f2f458` | 设备仓库 `feeds.toml` 的 `[[source]] name="sm8850"` 精确 pin ✅ |
| 设备/板级 | `repos/UniXNU-nx741j` | `32b9855fb815` | `board.toml [package]`；由主仓库 `devices` 记录引用 |
| 固件平台 | `/opt/work/uefi-sheng-main`（发布仓 `MengHanYoo/uefi-nx741j`） | `cfad0458567c` | `unixnu.local.toml [build] uefi = …` 只给路径，**无 commit pin（缺）** |

> 说明：`porting.toml` 里写过"`unixnu.lock` pins every source"，但当前检出中**没有 `unixnu.lock`**，
> 这是一个待补的固定项（见第 4 节）。

## 2. 已选组件（来自 `UniXNU/components.toml`，均带 revision）

`board.toml [components] include` 选中 6 个：`ArmDTPlatform`、`ArmCpu`、`SoftwareAES`、
`XnuPrelink`、`DarwinBoot`、`GopFramebuffer`（解析器不再补依赖）。

| 组件 | 仓库 | revision |
|---|---|---|
| `ArmCpu` | `UniXNU/ArmCpu` | `9066b53bbc82` |
| `ArmDTPlatform` | `UniXNU/ArmDTPlatform` | `417ff95d6a6e` |
| `SoftwareAES` | `UniXNU/SoftwareAES` | `4a45326da5c2` |
| `XnuPrelink` | `UniXNU/XnuPrelink` | `f876e3ee62e1` |
| `DarwinBoot` | `UniXNU/DarwinBoot` | `3325e352d7df` |
| `GopFramebuffer` | `UniXNU/GopFramebuffer` | `cf3b42979a9c` |

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

## 4. 待补的固定项（本次审计发现）

1. **`unixnu.lock` 缺失** → 框架仓库与 OS profile 没有 commit 级固定，重建时可能漂移；
2. **uefi 固件树未 pin** → `uefi-sheng-main` 仅以路径引用（当前 `cfad0458567c`），应写入 feeds/board 配置；
3. 组件 revision 来自 `components.toml`（离线可信），其上游远端**尚未逐个核对**（下次联网时校验）。

## 5. 待完成的硬件工作（与第 1 项缺失清单对应）

| # | 项目 | 阻塞点 |
|---|---|---|
| H1 | UFS → BlockIo | Mu DXE 里 `LocateProtocol/LocateHandleBuffer` 都拿不到 BlockIo；`HALIOMMU` 依赖 `DTBExtnProtocol` |
| H2 | `DTBExtnProtocol` 提供者 | stock 180 个驱动中未识别 |
| H3 | SMEM 读到 0 | DALSYS 依赖；XBL 数据未进 DXE |
| H4 | XNU 驱动 | 0/9（UFS、显示、触控、Wi-Fi、USB、音频、GPU…） |
