# NX741J 驱动记录（所选组件与设备 package）

依据：<https://unixnu.com/zh/porting/drivers>、<https://unixnu.com/zh/drivers/overview#procedure>。
本文件记录本移植**新增/修改的驱动行为**，与上游 `components.toml` 的官方组件区分：这些是本设备
package 内的驱动，不进主仓库组件目录。

## 1. 驱动清单

| id | 类型 | 文件 | 提供的接口 | 阶段 | 板级依赖 | 验证 |
|---|---|---|---|---|---|---|
| `SmemStubDxe` | 自建 DXE 驱动 | `firmware/smem-stub/SmemStubDxe.c/.inf` | `EFI_SMEM_PROTOCOL`（GUID `f4e5c7d0-d239-47cb-aacd-7f66ef763238`） | firmware | `nx741j.dsc` `[Components]` + `DXE.inc` + `APRIORI.inc`；`MemoryMapLib` 的 SMEM 需为 `Reserv` | 真机：`item 404 -> 0x… (8192 bytes)`，DALSYS 通过 |
| `KernelStubDxe` | 自建 DXE 驱动 | `firmware/kernel-stub/KernelStubDxe.c/.inf` | `EFI_KERNEL_PROTOCOL`（GUID `B5062BE7-170B-4A32-BE21-689262FF4399`），Version `0x00010005` | firmware | APRIORI 排在 `SmemStubDxe` 之前 | 真机：ChipInfo `spinlock.c+108` 消失；HALIOMMU 由 `Not Found` 变 `Not Ready` |
| 平台 PEI 补丁 | 源码修改 | `Silicon/Qualcomm/QcomPkg/Library/PlatformPeiLib/PlatformPei.c` | 兜底建 `gEfiSchedulerInterfaceHobGuid`，发布最小 `EFI_KERNEL_PROTOCOL`（Version + Lock/Event/Mutex/Sem/Spinlock） | firmware (PEI) | XBL 扫描为空时生效 | DALSYS `AutoGen.c +382` 消失 |
| 平台内存映射 | 源码修改 | `Platforms/ZTE/nx741jPkg/Library/MemoryMapLib/MemoryMapLib.c` | `SMEM Conv→Reserv`；新增 `RPMH_RSC 0x18900000/0x30000` | firmware (DXE) | —— | Rpmh 由 `translation fault` 变为其它失败（待查 `FAR=0x58`） |

## 2. 接口与行为约定

**SmemStubDxe**
- `AllocFunc(item, size, &p)`：按页分配并清零，页对齐；同一 item 复用，容量不足时重分配；item 表上限 1024（DALSYS 用 404）。
- `GetFunc(item, &size, &p)`：未分配则按需创建，避免调用方拿到 NULL。
- `Func2/Func3`：返回 `EFI_SUCCESS`（本固件无调用方）。
- 唯一消费者是 `DALSYSDxe`（全 `Binaries/nx741j` 扫描确认），它只调 `AllocFunc(404, 8192)`。

**KernelStubDxe**
- `Version = 0x00010005`（Lock API 引入版本），`Lock`/`Event`/`Mutex`/`Sem`/`Spinlock` 为桩实现；
- `Interrupt`/`Timer`/`Thread`/`MpCpu`/`WDog` 置 NULL，让调用方回落本地实现；
- 仅用于 BootShim 直跳路径（正常 XBL 交棒时由 XBL 提供真实调度器接口）。

## 3. 放置与构建

```sh
# 设备 package 内（firmware 树）
Platforms/ZTE/nx741jPkg/Drivers/SmemStubDxe/     # 源码 + INF
Platforms/ZTE/nx741jPkg/Drivers/KernelStubDxe/   # 源码 + INF

# 接入三处（install.sh 自动完成）
Include/DXE.inc        # FV 打包顺序
Include/APRIORI.inc    # DXE 调度顺序（KernelStub 在 SmemStub 前）
nx741j.dsc [Components]

# 构建（约 25 s，不经 GitHub）
bash firmware/smem-stub/build.sh        # 或 kernel-stub/install.sh 后同一构建

# 打包与刷机
bash firmware/smem-stub/pack.sh boot-uefi-<name>.img
powershell -File E:\default-workspace\z80u-collect\wsl\write_img.ps1 -Img … -Sectors 338
```

## 4. 与 UniXNU 组件模型的关系

| 项 | 说明 |
|---|---|
| 是否进 `components.toml` | 否。它们是设备 package 内的固件驱动，属 `build_stage = firmware` 的板级实现 |
| 与 `[components] include` 的关系 | 板级 `board.toml` 选的是 `ArmDTPlatform`/`ArmCpu`/`XnuPrelink`/`DarwinBoot`（内核/预链接/负载阶段），固件侧驱动由 FV 自带 |
| 后续如何变成组件 | 若某驱动需要跨设备复用，再按 `drivers/repositories` 规范独立成仓库并登记组件 |

## 5. 下一步驱动工作（按 row 的验收口径）

| 场景 | 做法 | 状态 |
|---|---|---|
| 新驱动报 `translation fault` | 对 DT 的 `reg` 与 `MemoryMapLib.c`，补 `AddDev` 行 | 已用于 `RPMH_RSC` |
| 新驱动报 `assert … autogen.c +N` | 反汇编该驱动构造链，找缺失协议/HOB，照 `KernelStubDxe` 打桩 | 已用于 DALSYS / ChipInfo |
| 驱动需要真实 SMEM 内容 | ChipInfo/Rpmh 这类静态链接 smem 库的驱动绕不过，需构造最小 SMEM 镜像 | ❌ 未做（当前最大缺口） |
| 板级 DT 节点缺失 | 补 `dts/board.dts` 或 `soc:dts/sm8850.dtsi` | 已按要求改为 include 结构 |
