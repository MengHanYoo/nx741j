> **状态提示（2026-11-08）**：本文是 11-06 的工作区快照，"层 2 卡在 Rpmh/TzDxeLA""层 3/4 未上机"
> 等结论已过时。当前实际状态以 `TASKS.md`（11-08 快照）、`STAGE2-CONFORMANCE.md`、
> `HARDWARE-INVENTORY.md` §5–6、`ANALYSIS-EFISP-VENDORBOOT.md` §20–24 为准。

# NX741J 工作区整合与归档（对齐 UniXNU 技术参考）

来源：<https://unixnu.com/zh/reference/>（技术参考索引）。本文把当前工作区、已完成的工作、各目录内容，
按该参考的「找配置 → 查规范 → 验证输出」三段式重新组织，便于续接和对外说明。

---

## 一、工作区映射（本地路径 ↔ UniXNU 概念）

参考页给出的项目结构：主仓库（工具与文档）、设备仓库（`board.toml` / `porting.toml` / `feeds.toml` /
`configs/`）、固件仓库（Mu-Silicium 分支）、输出目录（`out/<device>/<profile>/<config>/`）。

| UniXNU 概念 | 本项目实际路径 | 说明 |
|---|---|---|
| 主仓库 | `E:\default-workspace\z80u-collect\repos\UniXNU` | `./unixnu` CLI；`out/nx741j/ios-16.7.16-20H392/nx741j/` 是构建输出 |
| 设备仓库（配置） | `E:\default-workspace\z80u-collect\repos\UniXNU-nx741j` | `board.toml`、`porting.toml`、`feeds.toml`、`configs/nx741j_defconfig`、`dts/` |
| 设备仓库（本机代码/镜像） | `E:\default-workspace\z80u-collect\repos\nx741j` | 本仓库：`docs/`、`firmware/`、`image/`、`shim/`、`tools/`、`xnu/` |
| 固件仓库（uefi-sheng） | WSL `/opt/work/uefi-sheng-main` | `Platforms/ZTE/nx741jPkg`、`Binaries/nx741j`、`Silicon/Qualcomm`、`Mu_Basecore` |
| 构建输出 | `…/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j/` | `tree/`、`edk2-conf/`、`logs/firmware.log`、`payload/`、`boot/` |
| 固件产物 | `/opt/work/uefi-sheng-main/Build/nx741jPkg/DEBUG_CLANGPDB/FV/` | `SILICIUM_UEFI.fd`（4 MB）、`FVMAIN.Fv`、`FVMAIN_COMPACT.Fv` |
| 刷机镜像 | `E:\default-workspace\z80u-collect\boot-uefi-*.img` | 约 1.38 MB，337/338 扇区，见下文清单 |

本机独有的目录（UniXNU 参考里没有对应概念，但本仓库需要）：

| 路径 | 内容 |
|---|---|
| `repos/nx741j/firmware/smem-stub/` | SMEM 协议桩 `SmemStubDxe` + `install.sh` / `build.sh` / `pack.sh` |
| `repos/nx741j/firmware/kernel-stub/` | `EFI_KERNEL_PROTOCOL` 桩 `KernelStubDxe` + `install.sh` |
| `z80u-collect/` | 刷机脚本（`wsl/write_img.ps1` 等）、所有 `boot-uefi-*.img`、设备备份引用 |
| `z80u-collect/repos/ios-fuxi`、`ios-sweet`、`data-a-live` | iOS 侧镜像/pinfile 等配套工程 |

---

## 二、改动归属（按参考页第 1 节「找到配置或实现」）

| 改什么 | 参考页指向 | 本项目落点 | 当前状态 |
|---|---|---|---|
| 设备/SoC/组件选择 | 设备 `board.toml`、`feeds.toml`、`configs/` | `repos/UniXNU-nx741j/board.toml`、`configs/nx741j_defconfig` | 已配（sm8850 / canoe / Kaanapali） |
| 硬件资源（内存映射） | SoC DTSI + 板级 DTS | `uefi-sheng` 的 `Platforms/ZTE/nx741jPkg/Library/MemoryMapLib/MemoryMapLib.c` + 设备 `dts/` | 本次新增 `RPMH_RSC 0x18900000/0x30000`、SMEM 改 `Reserv` |
| 驱动行为 | 所选组件或设备 package | `firmware/smem-stub/`、`firmware/kernel-stub/`、`PlatformPei.c` | 三个桩已上机 |
| 内核行为 | profile/组件补丁序列 | `repos/nx741j/xnu/patches/`、`repos/nx741j/wsl/` | 属第 5 层，未开始 |
| 启动参数 | `[payload] kernel_args` | 设备仓库 payload 配置 | 待第 3 层 |
| 启动菜单/kit | payload 的 `EFI/UniXNU/unixnu.toml` | 计划放 efisp FAT（LUN4 @748894，3 MB） | 未开始（归档 P2） |
| 设备支持记录 | 设备 `porting.toml` + 主仓库 `devices.toml` | `repos/UniXNU-nx741j/porting.toml`（`status = "bringup-layer2"`） | 需在完成后更新 |

---

## 三、已完成工作（真机证据）

启动链第一段（bootloader → BootShim → FV）已闭环，第二段（DXE → BDS → UniXNU）在推进中。
按参考页的「启动链」四次交接理解，当前位于**交接二的前置**。

### 3.1 第 1 层：启动镜像（完成）

- kernel = `gzip(BootShim.bin + SILICIUM_UEFI.fd)`，`mtime=0`；`ramdisk_size=0`；**不追加 DTB**
- shim：`-DFD_BASE=0xFF300000 -DFD_SIZE=0x400000 -DREQUIRES_KERNEL_HEADER=1`（与 `porting.toml` 的 `fd` 一致）
- 打包与刷机脚本：`firmware/smem-stub/pack.sh`、`z80u-collect/wsl/write_img.ps1`

### 3.2 第 2 层：DXE 链（进行中，已解 4 个阻塞）

| # | 现象 | 根因（反汇编证据） | 解法 | 产物 |
|---|---|---|---|---|
| 1 | `SmemDxe` 入口 `EFI_NOT_FOUND` | BootShim 下 `0x81D00000` 无页表映射 | 自建 `EFI_SMEM_PROTOCOL` 桩（item 表需 ≥ 405，DALSYS 要 item 404 / 8192 B） | `firmware/smem-stub/` |
| 2 | DALSYS `AutoGen.c +382` | 构造函数 `0x1DD0` 依赖 `gEfiSchedulerInterfaceHobGuid` | `PlatformPei.c:BuildXblHobs()` 兜底建 HOB | PEI 改动 |
| 3 | ChipInfo `spinlock.c +108` | 走 `gEfiKernelProtocolGuid->Lock->InitRecursiveLock()`，无协议 | `KernelStubDxe` 提供 Version=0x00010005 的有效协议 | `firmware/kernel-stub/` |
| 4 | RpmhDxe `Data abort` | `0x18900000` 未映射 | `MemoryMapLib` 补 `RPMH_RSC` | 平台改动 |
| 5 | RpmhDxe `FAR=0x58`（进行中） | `gKernel == NULL` 时读 `+0x58`（`Lock`） | PEI 侧另建有效 `EFI_KERNEL_PROTOCOL`（Version + Lock/Event/Mutex/Sem/Spinlock） | 已验证仍复现，待定位 |

已通过的驱动：RuntimeDxe / ArmCpuDxe / ArmGicDxe / MetronomeDxe / TimerDxe / SmemStubDxe / DALSYSDxe /
HWIODxe / ChipInfoDxe / PlatformInfoDxe / HALIOMMUDxe（返回 `EFI_NOT_READY`，非致命）/ ULogDxe / IPCCDxe /
CmdDbDxe / PwrUtilsDxe / NpaDxe，后续在 RpmhDxe 异常。

### 3.3 平台事实（探测得到，避免重复踩坑）

| 地址 | 结论 |
|---|---|
| `0x81D00000` (SMEM) | 全 0，XBL 数据不可见 |
| `0x81A00000`~`0x81A20000` (XBL_DT) | 有数据（DT 字符串区），但 `d00dfeed` 零命中 |
| `0xFFBA0000` (FD_Reserved) | `d00dfeed` 零命中，无可用 XBL 残留 |
| `0x81C00000` (AOP) | **AP 侧读即 Data Abort**（XPU 保护），禁止探测 |
| DT `rpmh-rsc` | `0x18900000/0x10000 ×3`（drv-0/1/2） |
| DT `ipcc` | `0x01106000/0x1000`（被 `IPC_ROUTER_TOP 0x01100000/0x400000` 覆盖） |
| DT `cmd-db` | `0x81C60000`（落在 AOP 区内，CmdDbDxe 本身已通过） |

---

## 四、目录内容清单

```
E:\default-workspace\z80u-collect\
├─ repos\
│  ├─ nx741j\                     ← 本设备仓库（代码/文档）
│  │  ├─ docs\                    ← START-HERE / ARCHIVE / HANDOFF / PLAYBOOK 等
│  │  ├─ firmware\
│  │  │  ├─ smem-stub\            ← SMEM 协议桩 + install/build/pack 脚本
│  │  │  └─ kernel-stub\          ← Kernel 协议桩 + install 脚本
│  │  ├─ image\                   ← iosroot 镜像构建（BASE+补丁集）
│  │  ├─ shim\                    ← BootShim loader 相关
│  │  ├─ tools\                   ← bootimg/dtb/backup/gpu 等工具
│  │  ├─ xnu\                     ← 内核补丁
│  │  └─ wsl\                     ← Metal/GPU 相关脚本
│  ├─ UniXNU\                     ← 主仓库（CLI + out/ 构建输出）
│  ├─ UniXNU-nx741j\              ← 设备配置仓库（board/porting/feeds/configs/dts）
│  ├─ UniXNU-sm8850-common\
│  ├─ ios-fuxi\ ios-sweet\ data-a-live\
├─ wsl\                           ← write_img.ps1 / read_logfs*.ps1 / restore_*.ps1（EDL 操作）
├─ boot-uefi-*.img                ← 各阶段启动镜像（见第五节）
└─ *.ps1 / *.sh / *.py            ← 一次性实验脚本
```

WSL `/opt/work/uefi-sheng-main`（固件仓库）要点：

```
Platforms/ZTE/nx741jPkg/
├─ nx741j.dsc / nx741j.fdf
├─ Include/{DXE.inc, APRIORI.inc, RAW.inc}.{full}
├─ Drivers/{SmemStubDxe, KernelStubDxe, SmemDumpDxe}
├─ Library/{MemoryMapLib, ConfigurationMapLib}
└─ FdtBlob/kaanapali-zte-nx741j.dtb
Silicon/Qualcomm/QcomPkg/Library/PlatformPeiLib/PlatformPei.c   ← Scheduler HOB 兜底
Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc                ← DebugLib / SerialPortLib(FrameBuffer)
```

---

## 五、镜像与基线

| 镜像 | 内容 | 结论 |
|---|---|---|
| `boot-uefi-full.img` | 最初可用基线（卡 SmemDxe） | 归档 |
| `boot-uefi-smemstub.img/.2` | +SMEM 桩 | DALSYS 通过 |
| `boot-uefi-schedhob.img` | +Scheduler HOB 兜底 | DALSYS 构造通过 |
| `boot-uefi-kernelstub.img` | +Kernel 协议桩 | ChipInfo 通过 |
| `boot-uefi-rpmh.img` | +RPMH_RSC 映射 | 链到 RpmhDxe 异常 |
| `boot-uefi-gkernel.img` | +PEI 有效 `gKernel` | 仍 `FAR=0x58`（未解） |
| `boot-uefi-info.img` | 打开 DEBUG_INFO | 用于定位故障模块（待刷） |

**当前最好基线：`boot-uefi-rpmh.img`。** 设备 `boot_a` = LBA 119078，`uefi_a` = LBA 6，
`efisp` = LBA 748894（768 扇区），`logfs` = LBA 749919。

---

## 六、验证方法（对齐参考页第 3 节）

| 改动 | 验证 | 预期 |
|---|---|---|
| 固件/驱动桩 | `firmware/smem-stub/build.sh`（约 25 s）→ `pack.sh` → `write_img.ps1` | FV 4 MB 生成；屏幕出现对应 `install -> Success` / `item 404` / 新驱动 |
| 内存映射 | 屏幕 `Data abort` 消失 | `translation fault` 不再出现 |
| 设备状态 | `./unixnu status --json`（主仓库） | 设备/OS profile/输出目录一致 |
| 交接二 | BDS 菜单出现 + `UniXNUDxe` 注册 `Boot####` | 菜单可见，`unixnu:firmware` 选项存在 |
| 交接二第 3 条 | 卷上存在 `\EFI\UniXNU\unixnu.toml` | 启动管理器能解析配置 |

---

## 七、与参考模型的关系（设计判断）

参考页的定位是「统一的 UEFI 层加载设备所选的驱动」。本项目目前走的是**复用 ZTE vendor DXE 栈**的路线，
因此额外承担了 XBL 交棒数据的缺失（Scheduler/Kernel 协议、DTB extension、SMEM 内容）。

- 路线 A（当前）：继续补 vendor 依赖，优点是 UFS/PMIC/显示现成；
- 路线 B（参考页风格）：裁到最小驱动集（显示/存储/输入），放弃 vendor DAL/SMEM 体系。

建议先用路线 A 把交接二跑通；若 Rpmh/Clock/PMIC 连续出现同类 XBL 依赖，再评估切 B。

---

## 八、待办（按交接顺序）

1. **交接二第 1 条**：定位 RpmhDxe `FAR=0x58`——用 `boot-uefi-info.img` 的 `Loading driver at 0x...` 日志把
   `ELR=0xC16900A4` 落到具体模块；若确认是 `gKernel` 链，继续加固 PEI 的协议结构（函数指针重定位）。
2. **SMEM 内容**：XBL 数据已证实不可见，需按 `smem_init` 校验字段自建最小镜像（header/TOC/item 404）。
3. **交接二第 2/3 条**：确认 `UniXNUDxe` 注册 `Boot####`；在 efisp 做 FAT 卷放 `unixnu.toml` + payload。
4. **HALIOMMU**：`EFI_NOT_READY` 属非致命，等 DTB extension 有替代方案再处理。
5. **记录更新**：链跑通后更新 `porting.toml` 的 `[support] status` 与产物路径。
---

## 九、命令行（CLI）整合与当前步骤

来源：<https://unixnu.com/zh/reference/cli>。本机一律在
`/mnt/e/default-workspace/z80u-collect/repos/UniXNU` 下执行 `./unixnu <command>`，
设备仓库用 `-D ../UniXNU-nx741j`，固件仓库用 `--uefi /opt/work/uefi-sheng-main`。

### 9.1 命令使用情况

| 命令 | 参考页作用 | 是否已用 | 本项目实际做法 / 产物 |
|---|---|---|---|
| `defconfig` | 列出并复制 `configs/*_defconfig` 到 `out/<device>/<os>/<config>/.config` | ✅ | 已有 `out/nx741j/ios-16.7.16-20H392/nx741j/.config`（`CONFIG_XNU_NATIVE_*` 等） |
| `fetch` | 更新子模块 + 拉 git 源码，写设备仓库 `unixnu.lock` | ⚠️ 被动触发 | `build firmware` 会先 fetch 子模块（mu_basecore 等），本机网络下几分钟不动；**已绕开** |
| `tree` | 组装 `out/<device>/<os>/<config>/tree` | ✅ | `tree/package`、`tree/drivers/DarwinBoot` 已在，`firmware.log` 的 `PACKAGES_PATH` 引用它 |
| `ipsw info/fetch` | 打印/提取 IPSW 组件 | ❌ 本层不用 | 第 5/6 层（内核与 iosroot）才需要 |
| `build firmware` | 构建 Mu-Silicium 固件（DSC/FDF） | ✅ 概念完成、入口替换 | 直接调 BaseTools：`firmware/smem-stub/build.sh`（约 25 s）→ `Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd` |
| `build prelink/kernel` | 预链接与内核 | ❌ 未开始 | 第 5 层；已有旧产物 `kernel.development.pandora` + ADT |
| `build package` | UniXNU/DarwinBoot 包 | ❌ 未上机 | `UniXNU.efi`（96 KB）、`DarwinBoot` 已在 FV 内 |
| `build payload` | 负载（`EFI/UniXNU/unixnu.toml`） | ❌ 未开始 | 计划放 efisp FAT（LUN4 @748894，768 扇区）或 userdata |
| `build userdata` | Data 分区种子 | ❌ 未开始 | 已有 `iosroot-RAW-BUILD.img` |
| `bootimg` | 打包启动镜像（`--fd/--dtb/--ramdisk`） | ⚠️ **不用官方实现** | 设备配置 `[boot_image_kernel] append_dtb = true`，会把 DTB 追加到 gzip 流后，本机 ABL 直接 `Load Error`；改用 `firmware/smem-stub/pack.sh`（`gzip(shim+FV, mtime=0)` + `mkbootimg --header_version 4`，`ramdisk=0`） |
| `status [--json]` | 设备/输出/配置/模块来源 | ✅ 已跑 | device=`UniXNU-nx741j`，`modules=18`，`profiles`/`configuration`/`patch_order` 齐全；`build_config` 为 null（未用 `unixnu.local.toml`） |
| `clean` | 删 `tree/logs/kernel/package/boot/edk2-conf` | ❌ 不用 | 删了 `edk2-conf` 会破坏 25 s 增量构建，需重新 setup |
| `release` | `deps→preflight→patches→build→pack→verify` | ❌ | 未到发布阶段 |

### 9.2 关键选项 / 环境变量映射

| 参考页 | 本项目取值 |
|---|---|
| `-D` / `UNIXNU_DEVICE` | `../UniXNU-nx741j` |
| `--uefi` / `UNIXNU_UEFI` | `/opt/work/uefi-sheng-main` |
| `--edk2` / `UNIXNU_EDK2` | 未显式传；`build.sh` 用 `/opt/work/refsrc/edk2/BaseTools` |
| `--target` | `DEBUG`（产物目录 `DEBUG_CLANGPDB`） |
| `--out` / `UNIXNU_OUT` | 默认 `UniXNU/out` |
| `--fd` / `--dtb` / `--ramdisk` | 手工打包时对应 `SILICIUM_UEFI.fd`、无 DTB、`ramdisk=0` |

### 9.3 我们处在哪一步（对照 `build all` 顺序）

参考页：`build` 默认 `all` = kernel（含 prelink）→ package → payload → firmware；userdata 最先。

| CLI 步骤 | 六层链 | 状态 |
|---|---|---|
| `build userdata` | 第 6 层 | 未开始 |
| `build kernel`（含 prelink） | 第 5 层 | 未开始（有旧内核与 ADT 可复用） |
| `build package` | 第 3/4 层 | 未上机（`UniXNU.efi`、`DarwinBoot` 已在 FV） |
| `build payload` | 第 3 层 | 未开始（`unixnu.toml` 无处可放，交接二第 3 条未做） |
| `build firmware` | 第 1/2 层 | **进行中**：FV 可构建（25 s），真机 DXE 链推进到 `RpmhDxe`（`FAR=0x58`） |

**结论：卡在 `build firmware` 的"运行时"而不是"构建"本身**——产物能生成、能刷入、能跑起来，
但 DXE 链没跑完（第 2 层）。`build payload`/`build kernel` 都还没开始，且交接二第 3 条
（卷上的 `\EFI\UniXNU\unixnu.toml`）是它们的共同前置。

### 9.4 退出行为对照（参考页最后一张表）

| 条件 | 本项目 |
|---|---|
| 缺少 Mu-Silicium 仓库 | 已固定 `--uefi /opt/work/uefi-sheng-main`，满足 |
| 固件卷大小不匹配 | FD 固定 `0x400000`；`FVMAIN_COMPACT` 当前 1.41 MB / 4 MB，未触顶 |
| 负载超过 `size_mib` | 未涉及（payload 未构建） |
| 内核参数 >1000 字节 | 未涉及 |
| 缺少平台种子 `--unique-chip-id` | 第 3 层 payload 阶段才需要，待从设备读取 |
---

## 十、设备配置整合（board.toml vs 参考页字段）

来源：<https://unixnu.com/zh/reference/configuration>。本机设备仓库
`repos/UniXNU-nx741j/board.toml` 只有 42 行，下面逐表对照。

### 10.1 已配置（format 2）

```toml
schema_version = 2

[board]      name="nx741j" soc="SM8850" soc_source="sm8850"
             os_profile="ios-16.7.16-20H392" description="Nubia Z80 Ultra (NX741J), SM8850/alor" kit="nx741j"
[components] include=["ArmDTPlatform","ArmCpu","XnuPrelink","DarwinBoot"]
[kernel]     defconfig="nx741j_defconfig"
             config_fragments=["arch:configs/common.config","soc:configs/sm8850.config","os:configs/ios.config"]
             patch_series=["arch:patches/xnu/series","soc:patches/xnu/series","os:patches/xnu/series","device:patches/xnu/series"]
[device_tree] source="device:dts/board.dts" include_dirs=["soc:dts"]
[firmware]   config="nx741j" dsc="Platforms/ZTE/nx741jPkg/nx741j.dsc"
             fd="Build/nx741jPkg/{target}_CLANGPDB/FV/SILICIUM_UEFI.fd"
             packages=[… {tree}/package, {tree}/drivers/DarwinBoot, {edk2}]
[firmware.defines] DEVICE_MODEL="NX741J" ENABLE_SECUREBOOT="0"
[payload]    volume="NX741J" size_mib=64 loader="firmware" title="UniXNU" kernel_args=["-v","serial=3"]
[payload.adt] chip_id="207" unique_chip_id="required"
```

与参考页的对应关系：

| 参考表 | 本机 | 说明 |
|---|---|---|
| `[board]` | ✅ 6 个键齐全 | `soc_source="sm8850"`、`kit="nx741j"` |
| `[components].include` | ✅ 4 项 | 与 `unixnu status --json` 输出一致 |
| `[kernel]` | ✅ | defconfig + 3 个 config 片段 + 4 级 patch series |
| `[device_tree]` | ✅ | `device:dts/board.dts`，include `soc:dts` |
| `[firmware]` | ✅ 4/7 | 缺 `preflight`、`patches`、`submodules` |
| `[firmware.defines]` | ⚠️ 2 项 | 只放了 `DEVICE_MODEL`、`ENABLE_SECUREBOOT`；`FD_BASE/FD_SIZE/FD_BLOCKS` 目前写死在 `firmware/smem-stub/build.sh`（值来自 Mu-Silicium 的 `Resources/Configs/nx741j.toml`） |
| `[payload]` | ⚠️ 5/11 | 缺 `dram_size`、`panic_screen`、`trust_cache_sha256`、`tools`、`timeout`、`skip_setup_tool` |
| `[payload.adt]` | ⚠️ 2/9 | 只有 `chip_id`、`unique_chip_id`；缺 `tool`、`owned_memory_size`、`root_match_volume_uuid`、`cpu_regs`、`psci_method`、`packed_data_ramdisk`、`ipad_identity` |

### 10.2 完全缺失的表（后续步骤的前置）

| 表 | 用途 | 缺了会怎样 |
|---|---|---|
| `[ipsw]`（url/product/version/build/device_class/chip_id/board_id/fetch/sha256） | `unixnu ipsw fetch`、payload/kernel 的输入 | 第 5/6 层无法取内核与 iosroot |
| `[userdata]`（first_lba/last_lba/lba_bytes/windows） | `build userdata` 数据分区 | 第 6 层无法生成 Data 分区 |
| `[bootimg.image]`（base/kernel_offset/tags_offset/pagesize/header_version/kernel_compression） | `unixnu bootimg` 的镜像格式 | 目前只能手搓 `pack.sh`；`header_version=4`、`pagesize=4096` 等值还没进配置 |
| `[[bootimg.dtb_node]]` | 往启动镜像 DT 里注入属性 | 无法按设备注入引导参数 |
| `[kernel.prelink]`（tool/kernel/slide/roots/kernelcache…） | 预链接 | 第 5 层预链接不可配置 |

### 10.3 本地构建输入

参考页建议把路径写进 `unixnu.local.toml`（`[build] device/uefi/edk2/dtb/...`），
本机**没有这个文件**——`unixnu status --json` 的 `build_config` 为 `null`，全部靠命令行参数：

```
./unixnu build firmware -D ../UniXNU-nx741j --uefi /opt/work/uefi-sheng-main
```

`local.toml`（设备仓库内的组件源码覆盖）同样不存在。

### 10.4 与 Mu-Silicium 侧配置的关系（易踩）

`[firmware] config = "nx741j"` 指向 `/opt/work/uefi-sheng-main/Resources/Configs/nx741j.toml`，其中：

```toml
[boot_image_kernel]
kernel_compression = "gzip"
append_dtb         = true      ← 本机 ABL 不接受（Load Error）
```

这就是我们不用 `unixnu bootimg`、改用手工 `pack.sh` 的原因之一。参考页的
`[bootimg.image].kernel_compression` 可以在 board.toml 里覆盖这一行为，但目前没有配置。

### 10.5 进度判定

| 参考页配置项 | 覆盖步骤 | 状态 |
|---|---|---|
| `[board]`、`[components]`、`[kernel]`、`[device_tree]` | 解析与 `tree` 组装 | ✅ 已配置，`status` 可读 |
| `[firmware]` + `[firmware.defines]` | `build firmware` | ✅ 够用（构建走 `build.sh`） |
| `[payload]` + `[payload.adt]` | `build payload`、交接二第 3 条 | ⚠️ 关键字段缺（`dram_size`、`trust_cache_sha256`、`tools`、`timeout`） |
| `[ipsw]`、`[userdata]`、`[bootimg.image]`、`[kernel.prelink]` | `build kernel`/`userdata`、`bootimg` | ❌ 全缺 |

**结论：配置只覆盖到"构建固件"这一步，正好对应我们当前的实际进度（第 1/2 层）。**
要把交接二跑完并进入第 3 层，`[payload]` 需要先补全（尤其 `dram_size` 与 `trust_cache_sha256`），
而要跑 `build kernel`/`bootimg`，`[ipsw]`、`[bootimg.image]`、`[kernel.prelink]` 三张表必须补齐。
---

## 十一、组件目录（按参考页字段归档）

来源：<https://unixnu.com/zh/reference/components>。参考页的组件条目字段为：
`id`、`kind/path`、`repository/revision`、`legacy_name`、`function`、`scope`、`hardware`、
`os_abi`、`board_dependencies`、`verified_devices`、`dependencies`、`build_stage`、
`build`、`outputs`、`runtime_identifiers`。

本机 `[components] include`（`board.toml`）= `ArmDTPlatform`、`ArmCpu`、`XnuPrelink`、`DarwinBoot`；
`unixnu status --json` 显示解析后 `modules = 18`。下面按同一字段整理本移植涉及的组件。

### 11.1 已选官方组件

| id | kind | function | build_stage | 本机状态 | 备注 |
|---|---|---|---|---|---|
| `ArmDTPlatform` | driver | 发布 DT / ADT 平台信息 | 内核/负载 | ✅ 已选 | 提供设备树入口 |
| `ArmCpu` | driver | CPU 拓扑与启动 | 内核 | ✅ 已选 | SM8850 8 核 |
| `XnuPrelink` | prelink | 生成预链接内核 | prelink | ⚠️ 已选未跑 | 第 5 层才执行 |
| `DarwinBoot` | driver | XNU 引导（boot.efi 等价） | payload | ⚠️ 已构建未上机 | FV 内已有 `DarwinBoot.inf` |

`components.toml` 里与我们相关但**未选**的候选（按参考页“驱动适配清单”核对）：

| 能力 | 候选组件 | 为什么没选 |
|---|---|---|
| GPU（Adreno 840） | `Adreno740` 一类 GPU 接口组件 | 当前是第 2 层，GPU 属第 5/6 层 |
| Metal 前端 | `AdrenoMetal` | 同上；插件方案已在 `wsl/metal-patches` 预研 |
| GOP 帧缓冲 | `GopFramebuffer` | 本机用 Silicium 的 `SimpleFbDxe` + PCD 1216×2688 |

### 11.2 本移植新增组件（不属于上游 `components.toml`，属设备 package）

| 字段 | `SmemStubDxe` | `KernelStubDxe` |
|---|---|---|
| id | `SmemStubDxe`（本地） | `KernelStubDxe`（本地） |
| kind / path | `driver` / `firmware/smem-stub/` | `driver` / `firmware/kernel-stub/` |
| function | 提供 `EFI_SMEM_PROTOCOL`，替代原厂 `SmemDxe` | 提供 `EFI_KERNEL_PROTOCOL`（Qualcomm 调度器接口） |
| scope | 仅 DXE 内私有内存（item 表 1024，DALSYS 用 item 404 / 8192 B） | Lock/Event/Mutex/Sem/Spinlock 桩；Interrupt/Timer/Thread/MpCpu/WDog 为 NULL |
| hardware | 无（纯软件） | 无（纯软件） |
| board_dependencies | `nx741j.dsc`、`DXE.inc`、`APRIORI.inc` 三处接入；`MemoryMapLib` 的 SMEM 需为 `Reserv` | 无（APRIORI 排在 `SmemStubDxe` 前） |
| dependencies | `DxeCore`、`UefiBootServicesTableLib` | 同左 |
| build_stage | firmware | firmware |
| build | `install.sh` → `build.sh`（BaseTools，约 25 s） | `install.sh` |
| outputs | `Build/nx741jPkg/DEBUG_CLANGPDB/FV/Ffs/6F1B2C4D-…/…ffs` | `…/Ffs/2C6D8E41-…/…ffs` |
| runtime_identifiers | FFS `6F1B2C4D-8E37-4A95-B0D2-5C7E9A13F648`；协议 `f4e5c7d0-d239-47cb-aacd-7f66ef763238` | FFS `2C6D8E41-7B19-4F53-9A0E-5D3B1F84C672`；协议 `B5062BE7-170B-4A32-BE21-689262FF4399` |
| verified_devices | NX741J（DALSYS 通过、item 404 命中） | NX741J（ChipInfo `spinlock.c+108` 消失、HALIOMMU 由 `Not Found` 变 `Not Ready`） |

另有**两处平台补丁**，按组件口径记录：

| 补丁 | 文件 | build_stage | 作用 | 验证 |
|---|---|---|---|---|
| Scheduler HOB 兜底 | `Silicon/Qualcomm/QcomPkg/Library/PlatformPeiLib/PlatformPei.c:BuildXblHobs()` | firmware(PEI) | XBL 扫描为空时发布最小 `EFI_KERNEL_PROTOCOL`（Version 0x00010005 + Lock/Event/Mutex/Sem/Spinlock） | DALSYS `AutoGen+382` 消失 |
| 内存映射 | `Platforms/ZTE/nx741jPkg/Library/MemoryMapLib/MemoryMapLib.c` | firmware(DXE) | `SMEM Conv→Reserv`；新增 `RPMH_RSC 0x18900000/0x30000` | SMEM 保留；Rpmh 由 `translation fault` 变为其它失败（`FAR=0x58` 仍待查） |

### 11.3 按 build_stage 看进度

| build_stage | 组件/产物 | 状态 |
|---|---|---|
| firmware | `SmemStubDxe`、`KernelStubDxe`、PEI/DXE 补丁、`SILICIUM_UEFI.fd` | ⚠️ 构建完成，运行时卡 `RpmhDxe` |
| payload | `UniXNU.efi`、`DarwinBoot`、`unixnu.toml`（`[payload] volume=NX741J`） | ❌ 未打包/未上机 |
| prelink | `XnuPrelink`（`[kernel.prelink]` 未配置） | ❌ |
| kernel | `build kernel` + `[kernel] archives` | ❌ 未开始 |
| embedded | userdata / iosroot（`[userdata]` 未配置） | ❌ |

### 11.4 任务对照（下一步按组件口径）

| 任务 | 归属组件/阶段 | 验收 |
|---|---|---|
| 定位 `RpmhDxe FAR=0x58` | firmware（`KernelStubDxe`/PEI 协议） | 屏幕不再出现 `Data abort` |
| 自建最小 SMEM 镜像 | firmware（`SmemStubDxe` 或 PEI） | `smem_init` 通过、SMEM 读非 0 |
| 补 `[payload]` 缺字段 + `[bootimg.image]` | payload | `./unixnu bootimg` 能替代 `pack.sh` |
| efisp FAT 卷放 `\EFI\UniXNU\unixnu.toml` | payload / 交接二第 3 条 | 启动管理器能解析配置 |
| 补 `[ipsw]`、`[kernel.prelink]`、`[userdata]` | kernel/prelink/userdata | 第 5/6 层可构建 |
---

## 十二、启动配置（unixnu.toml / Kit.toml / 菜单）

来源：<https://unixnu.com/zh/reference/boot-configuration>。参考页规定：启动管理器先读**启动所在卷**的
`\EFI\UniXNU\unixnu.toml`，再扫描其他卷；kit 目录下 `Kit.toml` 用同一套 match/注入段。

### 12.1 与 NX741J 的对应关系

| 参考段/键 | 本机取值或计划 | 说明 |
|---|---|---|
| `[boot].timeout` / `default` | 5 / `xnu` | 手机会无输入设备，实际走 `fallback_timeout` |
| `[ui].title` | `NX741J` | 当 `title != "UniXNU"` 且 `logo=false` 时绘制文字 |
| `[ui].layout` | `auto` | **1216×2688 比值 452 < 800 → 自动竖屏**（正好符合手机面板） |
| `[ui].portrait_below` | 800 | 默认值；竖屏字形缩放取宽/270（1216 → 约 4） |
| `[[entry]] type="xnu"` | `path="Payloads/kernel.macho"`、`args="-v serial=3"`、`dtb="firmware"` | 参数与 `board.toml [payload].kernel_args` 一致 |
| `[[block_slice]]` | `partition="userdata"`、`name="iosroot"`、`writable=false`、`enabled=false` | 第 6 层挂 iosroot 时启用；长度按实际 GPT 填写 |
| `[[dt_patch]]` | `/chosen` 注入 `nx741j,bringup="layer2"` | 也可用于 `bootargs` 覆盖 |
| `[acpi]` | 未配置 | 本机走 DeviceTree 不装 ACPI 表 |
| `[[driver]] path="fv:<GUID>"` | 未配置 | 我们的桩驱动已在 FV 内按 APRIORI 自动加载，不需要菜单再点名 |
| `[[kit]]` + `[kits].autoload` | 未配置 | 设备 kit 为 `nx741j`，若要按 `[match]` 注入驱动/overlay 时再加 |

### 12.2 Kit.toml 的 match 条件（本机可用的判据）

| match 键 | NX741J 可用值 | 来源 |
|---|---|---|
| `arch` | `AARCH64` | 固定 |
| `fdt_compatible` | 板上 DT 根 `compatible` 中的字符串（如 `qcom,kaanapali` / `nubia,nx741j` 一类） | 需从 `kaanapali-zte-nx741j.dtb` 根节点读取后填写 |
| `firmware_vendor` | `gST->FirmwareVendor` 子串 | 固件实际值待打印 |
| `requires_protocol` | 例如 `f094...`（SMEM/Kernel 桩的 GUID） | 我们已安装 `f4e5c7d0-…`、`B5062BE7-…` |

### 12.3 已落盘的草案

`firmware/payload/unixnu.toml`：按参考页字段写好 NX741J 的
`[boot]`/`[ui]`/`[[entry]]`/`[[block_slice]]`/`[[dt_patch]]`，`iosroot` 切片先 `enabled=false`，
等第 6 层再打开。正式 payload 由 `./unixnu payload` 生成，草案用于合并设备相关项。

### 12.4 验证步骤（照参考页）

1. `./unixnu payload` 重新生成 payload（需先补 `[payload]` 缺字段：`dram_size`、`trust_cache_sha256`、`tools`、`timeout`）；
2. `./unixnu bootimg` 打包（目前仍用 `pack.sh`，因为 Mu 配置 `append_dtb=true` 与本机 ABL 冲突）；
3. 用 `mtype -i <payload.img> ::/EFI/UniXNU/unixnu.toml` 检查卷内配置；
4. 启动后核对菜单行为：无输入设备时应走 `fallback_timeout`；启动失败应返回菜单且不再倒计时；
   菜单会显示配置来源、菜单项、注入摘要和第一条错误。

### 12.5 进度判定

| 项 | 状态 |
|---|---|
| unixnu.toml 草案 | ✅ 已写（`firmware/payload/unixnu.toml`） |
| payload 打包 | ❌ 未做（无 payload 卷，`[payload]` 字段不全） |
| 卷内布局（Payloads/、Kexts/、Drivers/） | ❌ 未建立 |
| `mtype` 校验 | ❌ 未做（尚无 payload.img） |
| 菜单实际显示 | ❌ 未验证（DXE 未跑到 BDS） |

**结论：启动配置这一层处于“文档与草案就绪、打包未开始”的状态**，它的前置仍是第 2 层跑通
（DXE → BDS）和交接二第 3 条（可读卷）。按参考页顺序，接下来应先补 `[payload]` 字段并生成
payload 卷，再回到固件链把 BDS 打通，二者汇合后才能真正看到菜单。
---

## 十三、输出布局（out/ 实际内容对照）

来源：<https://unixnu.com/zh/reference/output-layout>。参考页的格式 2 布局为
`out/<device>/<os_profile>/<config>/` 下 `.config`、`tree/`、`logs/`、`device-tree/hardware.dtb`、
`kernel/kernel.macho`、`package/Build/Out/`、`payload/payload.img|json`、`boot/boot.img|json`、`userdata/…`。

### 13.1 实际目录（`out/nx741j/ios-16.7.16-20H392/nx741j/`）

| 条目 | 实际 | 对应参考页 | 说明 |
|---|---|---|---|
| `.config` | 790 B | ✅ | 由 `defconfig` 生成 |
| `tree/` | 有 | ✅ | 模块目录 + `package/` |
| `logs/` | 有（`firmware.log` 1.8 MB） | ✅ | 见 13.3 |
| `device-tree/` | 有 | ⚠️ | 应有 `hardware.dtb`；本机固件版 DT 实际在固件树的 `FdtBlob/kaanapali-zte-nx741j.dtb` |
| `package/` | 有 | ✅ | `Build/Out/` 下是 UniXNUPkg 产物 |
| `payload/` | `payload.img` **64 MiB** + `payload.json` | ✅ | 旧构建产物，见 13.2 |
| `boot/` | `boot.img` **69 MB** + `boot.json` | ⚠️ | 旧构建产物，参数与当前方案不符 |
| `kernel/` | **不存在** | ❌ | 未跑 `build kernel` |
| `userdata/` | **不存在** | ❌ | 未跑 `build userdata` |
| `edk2-conf/` | 有 | ➕ 额外 | EDK II 的 `target.txt`/`tools_def.txt`/`build_rule.txt`，`build.sh` 依赖它；`unixnu clean` 会删 |
| `toolchain/` | 有 | ➕ 额外 | clang/lld-link/llvm-lib/llvm-rc 的符号链接 |
| `state.json`、`feeds/`、`ipsw/` | **不存在** | ❌ | 从未用 `-D` 记忆设备或 `fetch`/`ipsw fetch` |

### 13.2 sidecar 实录（旧产物，可作为对照基线）

`boot/boot.json`：

```json
{ "kernel_sha256": "a7c2ba9a…", "ramdisk_bytes": 67108864, "header_version": 4,
  "kernel_address": "0x8000", "tags_address": "0x1e00000",
  "image_sha256": "9c93dc11…", "image_bytes": 69287936,
  "fd_sha256": "bd32901f…", "dtb_sha256": "a5c109c0…",
  "shim_bytes": 192, "hardware_tested": false }
```

**两处与当前正确方案冲突**，正好对应 HANDOFF 里的两次失败：

| 字段 | 旧值 | 现在必须 | 后果 |
|---|---|---|---|
| `ramdisk_bytes` | 67,108,864（64 MiB 内置 ramdisk） | **0** | GKI 约定 ramdisk 在 init_boot；内置会导致 `Load Error` |
| `kernel_address` | `0x8000` | **`0xFF300000`** | shim 的 `text_offset` 必须是 FD 基址 |

`payload/payload.json`：

```json
{ "kernel_sha256": "f01be28d…", "adt_sha256": "7c1a9fbf…", "kernel_args": "-v serial=3",
  "trust_cache_sha256": "63e01c03…", "payload_bytes": 67108864, "hardware_tested": false }
```

`payload_bytes = 64 MiB` 与 `board.toml [payload].size_mib = 64` 一致；`hardware_tested: false` 说明从未上机。

### 13.3 日志对应

| 参考页日志 | 本机是否存在 | 用途 |
|---|---|---|
| `firmware.log` | ✅ 1.8 MB（最近 22:46 那次） | EDK II 固件构建 |
| `basetools.log` | 未见 | BaseTools 首次编译 |
| `kernel.log` / `prelink.log` / `package.log` | ❌ | 第 3~5 层 |
| `payload.log` / `payload-adt.log` | ❌ | payload 打包 |
| `userdata.log` | ❌ | 第 6 层 |

**本项目实际使用的固件日志与参考页一致**：`firmware.log` 头部记录 `WORKSPACE`/`PACKAGES_PATH`/`EDK_TOOLS_PATH`/`CONF_PATH`，
`build.sh` 就是按这份环境复现的。

### 13.4 `unixnu clean` 的影响（本机注意）

参考页：`clean` 删 `tree`、`logs`、`kernel`、`package`、`boot`、`edk2-conf`，保留已拉取源码与 IPSW 成员。
本机的 `edk2-conf` 是 `build.sh` 的必需输入（`CONF_PATH`），一旦被删，25 秒增量构建会退化为需要重新
`setup/update`（会触发 GitHub 拉取），因此**本项目不执行 `unixnu clean`**。

### 13.5 进度判定

| 输出类别 | 状态 | 依据 |
|---|---|---|
| 构建树与配置（`.config`/`tree`/`edk2-conf`） | ✅ | 有 |
| 固件（`firmware.log` + `Build/.../SILICIUM_UEFI.fd`） | ✅ | 4 MB 产物 + 1.8 MB 日志 |
| payload（`payload.img`/`.json`） | ⚠️ 有过期产物 | 64 MiB、`hardware_tested: false` |
| boot（`boot.img`/`.json`） | ⚠️ 有不合格产物 | ramdisk 64 MiB、`kernel_address 0x8000` |
| kernel / userdata | ❌ | 目录都不存在 |
| 当前真正在用的启动镜像 | 在 `z80u-collect/boot-uefi-*.img`（手工 `pack.sh`，ramdisk=0、无 DTB） | 与 `boot/boot.img` 不是一回事 |

**结论**：`out/` 里能证明的进度与前面章节一致——固件构建可用、payload/boot 有历史产物但参数已过时
（正是 HANDOFF 记录的那两次失败），kernel 与 userdata 从未构建。下一层要做的就是在修正后的参数下
重新生成 `payload/` 与 `boot/`（或明确声明改用 `pack.sh` 手工链路，并把过时的 `boot.json` 归档）。
---

## 十四、对照技术参考的完成度审计

来源：<https://unixnu.com/zh/reference/>。参考页把工作分成三步：**1 找到配置或实现 → 2 查阅对应规范 →
3 验证受影响的输出**，并给出 26 个规范页面（配置与输出 7、启动原理 5、驱动开发 6、XNU 内核 4、验证与贡献 4）。

### 14.1 文档整合覆盖（6 / 26）

| 分区 | 页面 | 状态 |
|---|---|---|
| 配置与输出 | 查找操作与字段（索引） | ✅ 本文一~八节 |
| 配置与输出 | 命令行 | ✅ 第九节 |
| 配置与输出 | 设备配置 | ✅ 第十节 |
| 配置与输出 | 组件目录 | ✅ 第十一节 |
| 配置与输出 | 启动配置 | ✅ 第十二节 |
| 配置与输出 | 输出布局 | ✅ 第十三节 |
| 配置与输出 | 术语表 | ❌ 未整合 |
| 启动原理 | 启动链 | ⚠️ 做了对照分析，未成节 |
| 启动原理 | 预链接 / Payload / userdata 布局 / 完整性与启动策略 | ❌ |
| 驱动开发 | 开发驱动 / 仓库与组件 / UEFI 驱动 / 内置 XNU 驱动 / Kext 包 / 设备树匹配 | ❌ |
| XNU 内核 | 源码与补丁 / 补丁参考 / 4 KiB 页表 / 启动参数 | ❌ |
| 验证与贡献 | QEMU / 故障排查 / 仓库布局 / 文档维护 | ❌ |

### 14.2 按参考页第 3 节的验证表判定

| 改动类别 | 参考页预期 | 本机实际 | 完成 |
|---|---|---|---|
| 内核、驱动或补丁 | 构建内核 + 按 bring-up 检查 | 未构建内核；固件侧驱动桩已上机验证 | ❌ |
| 固件或 payload | 更新的 FD、payload、启动镜像使用指定输入 | FD ✅ 4 MB；启动镜像 ✅ 手工 `pack.sh`；payload ⚠️ 过期 64 MiB | ⚠️ |
| 启动参数 | 打包后 `args` 与预期一致 | `payload.json` 中 `-v serial=3`，但未上机 | ⚠️ |
| 公共 UEFI 行为 | QEMU 场景出现预期日志 | 未用 QEMU（缺 ArmVirtPkg，见归档 P0 记录） | ❌ |
| 适配记录或文档 | 记录验证、文档构建通过 | 文档已写；`porting.toml` 状态未更新 | ⚠️ |

### 14.3 逐层结论

| 层 | 状态 | 证据 |
|---|---|---|
| 1 启动镜像 | ✅ 完成 | `gzip(shim+FV)`、`ramdisk=0`、无 DTB、`text_offset=0xFF300000` |
| 2 UEFI 固件 | ⚠️ 进行中 | DXE 链到 `RpmhDxe`（`FAR=0x58`）；已过 DALSYS/HWIO/ChipInfo/ULog/IPCC/CmdDb/PwrUtils/Npa |
| 3 UniXNU | ❌ 未上机 | `UniXNU.efi` 已在 FV，未注册 Boot####、未打包 payload 卷 |
| 4 DarwinBoot | ❌ 未上机 | 二进制已构建 |
| 5 内核 | ❌ 未开始 | `out/.../kernel/` 不存在，`[kernel.prelink]` 未配置 |
| 6 用户态 | ❌ 未开始 | `out/.../userdata/` 不存在，`[userdata]` 未配置 |

### 14.4 审计结论

**没有全部完成。** 准确表述是：

- **文档整合**：6/26，集中在「配置与输出」分区（命令行、设备配置、组件目录、启动配置、输出布局全部完成）；
- **工程进度**：第 1 层完成，第 2 层进行中（4 个阻塞已解、1 个待解），第 3~6 层未开始；
- **配置完备度**：`board.toml` 只覆盖到 firmware 阶段，`[ipsw]`/`[userdata]`/`[bootimg.image]`/`[kernel.prelink]` 四张表全缺；
- **产物**：`out/` 中的 payload/boot 是参数已过期的历史产物，真正可用的启动镜像在 `z80u-collect/boot-uefi-*.img`。

### 14.5 补齐顺序（按参考页依赖）

1. 第 2 层收尾：`RpmhDxe FAR=0x58`（用开了 DEBUG_INFO 的镜像定位到模块）→ Clock/PMIC/UFS → BDS；
2. 交接二第 3 条：补 `[payload]` 缺字段 → `./unixnu payload` → 卷内 `EFI/UniXNU/unixnu.toml`（草案已写）；
3. 文档侧补「启动链 / Payload / userdata」三页的对照，使启动原理分区完整；
4. 第 5 层前补 `[ipsw]`、`[kernel.prelink]`、`[bootimg.image]`、`[userdata]`。
---

## 十五、技术参考第 2 栏（查阅对应规范）全量归档

来源：<https://unixnu.com/zh/reference/> 第 2 节"查阅对应规范"，共 19 个页面，全部抓取成功。
下表按栏目列出**规范要点**与**本项目落点**。

### 15.1 构建工具

| 页面 | 规范要点 | 本项目落点 / 状态 |
|---|---|---|
| 命令行 | `build <step>` 六步（kernel/package/firmware/payload/prelink/userdata）；`--uefi`=Mu-Silicium、`--edk2`=Mu_Basecore；`bootimg --fd/--dtb/--ramdisk`；`status --json` 给 components/patch_order | ✅ 第九章；`build.sh` 绕过 `build firmware` 的 fetch，`pack.sh` 替代 `bootimg` |
| 设备配置 | 格式 2 需 `schema_version=2`；`[board] soc/kit`、`[payload] volume/size_mib/kernel_args/trust_cache_sha256`、`[kernel.prelink] roots/slide`；`unixnu.local.toml` 存路径 | ✅ 第十章 + 已补 `[payload]`/`[bootimg.image]`；`unixnu.local.toml` 已建并验证 |
| 输出布局 | `out/<device>/<os>/<config>/{tree,kernel,payload,boot}`；`payload.json`/`boot.json` sidecar 字段；`clean` 删 tree/logs/kernel/package/boot | ✅ 第十三章；实测 `boot.json` 记录旧参数（ramdisk 64 MiB、kernel_address 0x8000）即两次失败证据 |

### 15.2 组件

| 页面 | 规范要点 | 本项目落点 / 状态 |
|---|---|---|
| 组件目录 | `components.toml` 字段 id/kind/path/hardware/os_abi/board_dependencies/build_stage/outputs/runtime_identifiers；设备 include 替换继承数组 | ✅ 第十一章；当前只选 4 个（ArmDTPlatform/ArmCpu/XnuPrelink/DarwinBoot），GPU/Metal/GOP 组件未选 |
| 驱动仓库 | 官方仓 UniXNU/Adreno740、ShengUfs、ArmDTPlatform、ArmCpu、DarwinBoot；一驱一仓、带 `unixnu` topic；社区驱动以子模块链接 | ⚠️ 我们的桩驱动是设备 package 内实现（`firmware/`），未独立成仓；符合"未跨设备复用前不必成仓"的边界 |

### 15.3 驱动接口

| 页面 | 规范要点 | 本项目落点 / 状态 |
|---|---|---|
| UEFI 驱动 | `.inf` 需 `FILE_GUID` + `MODULE_TYPE=UEFI_DRIVER`；进 DSC `[Components]` 与 FDF `[FV.*]`；`[[driver]] path="fv:<GUID>"` | ✅ `SmemStubDxe`/`KernelStubDxe` 均按此接入（DSC + DXE.inc + APRIORI.inc） |
| XNU 驱动 | `drivers/<Name>/Name.cpp`（IOService 子类）；按 `.cpo.json` 复刻 ABI（magic 0xfeedfacf、CPU 0x0100000c、MH_OBJECT）；静态库入 `[kernel] archives` 并 `-force_load` | ❌ 第 5 层，未开始 |
| kext 包 | `.kext` bundle（Info.plist + MacOS/可执行）；`[[entry.kext]]`；校验 MH_KEXT_BUNDLE(11)；trust cache 必含 CD hash；**需实现 revision 2 的加载器**（发布版 DarwinBoot 拒绝暂存 kext） | ❌ 未开始；若要走 kext 路线需先确认 DarwinBoot revision |
| 设备树匹配 | personality 键 `IOClass/IOProviderClass/IONameMatch/IOPropertyMatch/IOProbeScore/IOResourceMatch`；provider 为 IOPlatformDevice（`arm-io` 子节点）/IOResources/IOMedia；`reg` 小端 u64 对 | ⚠️ 已确认固件 FDT 根为 `qcom,canoe*`，无 `nubia,nx741j`；DT 匹配要等 ADT/内核阶段 |

### 15.4 XNU

| 页面 | 规范要点 | 本项目落点 / 状态 |
|---|---|---|
| 源码与补丁 | iPadOS 17.7.11 固定 `xnu-10063.141.1`/Darwin 23.6.0；`patch_series` 有序，前缀 arch/soc/os/device/component；`ARM64_LINK_ADDR=0xfffffff007004000` | ⚠️ `arch` 序列解析出 53 条；`soc`/`device` 序列为空（`repos/nx741j/xnu/patches/0103–0105` 未迁入，待第 5 层判定） |
| 补丁参考 | 按命名排序应用；`zzw-/zzy-/zzz-` 最后；关键补丁 `native-4k-*`、`armvirt-platform-personality`、`native-trust-cache-runtime`、`zzz-boot-progress` | ⚠️ 已识别设备 4K 相关补丁位置；未实际应用 |
| 4 KiB 页表 | `ARM_PGSHIFT=12`、`ARM_PGBYTES=4096`、`BOOTSTRAP_TABLE_SIZE`；`native-4k-user-abi` 设 commpage/mmap shift 12；`native-4k-signature-hash-cache` 派生 4K 哈希 `csb_native4k_hashes`；`zzx-pmap-sync-64bit-pa` 支持 >4 GiB DRAM | ❌ 未开始；SM8850 原生 4 KiB，属必经路径 |

### 15.5 启动与镜像格式

| 页面 | 规范要点 | 本项目落点 / 状态 |
|---|---|---|
| 启动链 | kernel 字段 = gzip(BootShim+固件卷)，后接 DT；`-DFD_BASE/-DFD_SIZE` 按设备；`UniXNUDxe` 注册 `Boot####`；`DARWIN_BOOT_ARGS` 在 0x6c 处 1024 字节 | ✅ 第 1 层完全吻合（`0xFF300000/0x400000`、ramdisk=0、无 DTB）；交接二未完成 |
| 预链接 | 源码内核 + 原始 kernelcache(LZFSE/IM4P) → `kernel.macho`；`__PRELINK_TEXT→__TEXT`、`__PLK_DATA_CONST→__DATA_CONST` 重绑指针 | ❌ 未开始（缺 `[kernel.prelink]` 与 kernelcache 固定哈希） |
| Payload | FAT 卷作 ramdisk；卷内 `/EFI/UniXNU/unixnu.toml`、`Payloads/kernel.macho`(.adt/.trustcache)；ADT 预留 `/chosen/memory-map/RAMDisk\|PackedRAMDisk\|TrustCache`；**命令行 ≤1000 字节** | ⚠️ 卷已生成（64 MiB，`unixnu.toml` 已验证 title/args）；ADT 占位符与 1000 字节限制未核对 |
| userdata 布局 | magic `SHNGROOT`、头 4096 字节补零到 1 MiB、首窗口自 0x100000、1 MiB 对齐；窗口 `OS`/`SystemOS`；`fastboot flash userdata` | ❌ 未开始；`/chosen/root-matching` UUID 必须等于 OS 窗口卷 UUID |
| 完整性与启动策略 | `--trust-cache` + `[payload] trust_cache_sha256`；`/chosen/amfi-only-platform-code`；`native-4k-signature-hash-cache`；`offline-owner-root-*` 与 `kits/VerifiedRoot` | ⚠️ 已把本地 trustcache 哈希加入白名单；AMFI 策略未验证 |

### 15.6 项目结构

| 页面 | 规范要点 | 本项目落点 / 状态 |
|---|---|---|
| 仓库布局 | 主仓库含 `components.toml`/`unixnu`/`profiles`/`drivers`/`kits`/`patches`；`UniXNU-<soc>-common` 放 DTSI；`UniXNU-<device>` 放板级 DTS 与打包 | ⚠️ 已按此布局（`UniXNU` + `UniXNU-nx741j` + `UniXNU-sm8850-common`），缺独立 `uefi-nx741j` 仓（固件在 WSL `/opt/work`） |
| 术语表 | ADT、AMFI、BootShim、personality、PGZ、预链接、SHNGROOT、SystemOS、DSC/FDF/INF | ✅ 已贯穿各章；建议新人先看 ADT/personality/SHNGROOT 三词 |

### 15.7 从规范里读出的硬约束（对本项目影响最大）

1. **payload 卷必须含 ADT 且预留 `/chosen/memory-map/*` 占位**，否则加载器返回 `EFI_COMPROMISED_DATA`；
2. **命令行 ≤1000 字节**，参数须匹配 `[A-Za-z0-9_.\-]+`——我们现在的 `-v serial=3` 远低于上限，但后续调试参数要预留空间；
3. **4 KiB 页是 SM8850 的必经路径**（`ARM_PGSHIFT=12` + 4K user ABI + 4K 代码签名哈希），不是可选项；
4. **kext 路线依赖 DarwinBoot revision 2**，发布版加载器会拒绝暂存 kext；
5. **userdata 的 root-matching UUID 必须等于 OS 窗口卷 UUID**，否则 XNU 挂不上根；
6. **`UniXNURamDiskDxe` + `DiskIoDxe` + FAT 必须进固件**，payload 卷才能在 UniXNU 读配置前被识别（我们 FV 已有这三者）。

### 15.8 覆盖度与差距

| 指标 | 数值 |
|---|---|
| 本次抓取页面 | 19 / 19（全部成功） |
| 整合进本文档的参考页 | 累计约 21 / 26（余：开发驱动、启动参数、QEMU、故障排查、文档维护） |
| 规范中本项目已落实 | 构建工具三页、组件目录、UEFI 驱动、启动链 |
| 规范中明确未开始 | XNU 驱动、kext、预链接、4 KiB 页表、userdata、完整性策略 |
| 最大单点风险 | 第 2 层 DXE 未通（`RpmhDxe FAR=0x58`），其上所有依赖规范都无法验证 |
---

## 十六、技术参考第 3 栏（验证受影响的输出）归档

来源：<https://unixnu.com/zh/reference/> 第 3 节。该节要求：先在主仓库选定设备跑
`./unixnu status --json` 核对**设备、OS profile、配置、模块来源、输出目录**，再按改动类别执行验证；
失败时按[故障排查](https://unixnu.com/zh/reference/troubleshooting)处理，并保存**命令、输入版本、相关日志**。

### 16.1 前置核对（已执行）

```sh
cd /mnt/e/default-workspace/z80u-collect/repos/UniXNU
./unixnu status --json
```

| 核对项 | 实际值 | 结论 |
|---|---|---|
| 设备 | `../UniXNU-nx741j` | ✅ |
| OS profile | `ios-16.7.16-20H392` | ✅ |
| 配置 | `.config`（790 B）+ `nx741j_defconfig` + 四级 config_fragments | ✅ |
| 构建输入 | `unixnu.local.toml`（`build_config` 非空） | ✅ |
| 模块来源 | 18 个模块，组件 revision 已固定 | ✅ |
| 输出目录 | `out/nx741j/ios-16.7.16-20H392/nx741j` | ✅ |

### 16.2 五类改动的验证（逐条对照本机证据）

| 参考页要求 | 验证方法 | 本机执行记录 | 结论 |
|---|---|---|---|
| **内核、驱动或补丁**：内核与预链接成功，受影响检查点单独记录 | 构建内核 → 按启动验证顺序检查 | 内核未构建（`out/.../kernel/` 不存在，`payload` 报 `kernel not found`）；固件侧驱动桩有真机记录：DALSYS `item 404`、ChipInfo `spinlock.c+108` 消失 | ⚠️ 驱动侧已验证，内核侧未开始 |
| **固件或 payload**：更新的 FD、payload 和启动镜像使用指定输入 | [构建与启动](https://unixnu.com/zh/building) | FD 4,194,304 B（`FVMAIN_COMPACT` 1.41 MB / 4 MB）；payload.img 64 MiB，卷标 `NX741J`，`mtype` 已验 `unixnu.toml`；启动镜像由 `pack.sh` 产出 1.38 MB（ramdisk=0、无 DTB、`text_offset=0xFF300000`） | ✅ 输入一致；`boot.json` 仍是过期记录（第 13 章） |
| **启动参数**：打包后的 `args` 与预期命令行一致 | [检查 payload 配置](https://unixnu.com/zh/xnu/boot-arguments#apply-arguments) | `mtype -i payload.img ::/EFI/UniXNU/unixnu.toml` → `args = "-v serial=3"`，与 `board.toml`/`unixnu.local.toml` 三层一致 | ✅ |
| **公共 UEFI 行为**：所选场景出现预期日志标记 | [QEMU 操作步骤](https://unixnu.com/zh/reference/qemu#procedure) | 未使用 QEMU：本机 `nx741jQemuPkg` 缺 `ArmVirtPkg`（无 `QemuVirtMemInfoLib`），串口无输出（归档 P0 已记录） | ❌ 不可用 |
| **适配记录或文档**：记录验证和文档构建通过 | [记录检查](https://unixnu.com/zh/porting/records#procedure)、[站点构建](https://unixnu.com/zh/contributing/documentation#procedure) | `porting.toml` 的 `[support].status` 已改 `bringup-layer2-dxe-chain`；文档 5 份（START-HERE / ARCHIVE / INTEGRATION / DRIVERS / README） | ⚠️ 记录已更新但未定稿，站点未构建 |

### 16.3 失败处置（参考页要求的保存项）

| 参考页要求 | 本机兑现方式 |
|---|---|
| 按故障排查处理 | 已知故障模式已形成套路：`translation fault` → 对 DT `reg` 补 `MemoryMapLib`；`assert autogen +N` → 反汇编构造链补协议桩；`spinlock/lock handle` → 补 `gEfiKernelProtocolGuid` |
| 保存命令 | 构建/打包/刷机全部脚本化（`firmware/smem-stub/{build,pack}.sh`、`wsl/write_img.ps1`），命令可复现 |
| 保存输入版本 | `firmware.log` 头部记录 `WORKSPACE`/`PACKAGES_PATH`/`EDK_TOOLS_PATH`/`CONF_PATH`；`payload.json`/`boot.json` 记录各输入 sha256；固件 FDT 记录 sha256 `a5c109c0…` |
| 保存日志 | `out/.../logs/firmware.log`（1.8 MB）；真机屏幕输出逐轮记录在本文档与 START-HERE |

### 16.4 缺口与建议

| 缺口 | 影响 | 建议 |
|---|---|---|
| 无 QEMU 验证通道 | 公共 UEFI 行为只能靠真机屏幕 | 补 `ArmVirtPkg`/`QemuVirtMemInfoLib`，或改用 `nx741jQemuPkg` 的替代串口方案 |
| `boot.json` 过期 | 该 sidecar 与当前 `pack.sh` 产物不一致 | 用 `pack.sh` 重新生成时同步写 sidecar，或明确标注"手工链路" |
| 内核未构建 | 第 1 行无法验收 | 先跑 `./unixnu build kernel` 或提供 `--kernel FILE` |
| 记录未定稿 | 第 5 行只能算 ⚠️ | 第 2 层跑通后更新 `porting.toml` 并补 `verified_devices` |

### 16.5 结论

第 3 栏的**前置核对**（status）与**三条验证**（固件输入、启动参数、驱动行为）已经有真机或产物级证据；
**两条未完成**：QEMU 公共行为验证（缺依赖）与内核构建验证（属第 5 层）。
这与第十四节的审计一致——验证方法本身是通的，缺的是可验证的对象。
---

## 十七、第 5 层（内核）开工前检查清单

**决策**：`soc/` 与 `device/` 补丁序列**保持为空**，设备仓库的 `xnu/patches/0103–0105` 不迁入。
理由：`0104`/`0105` 已证实与主仓库 `native-4k-user-abi.patch`、`native-4k-signature-hash-cache.patch`
逐行相同，已由 `arch` 序列应用；重复迁入会导致补丁叠加冲突。

### 17.1 第一项检查：`0103` 的格式比对（唯一未定项）

```sh
cd /mnt/e/default-workspace/z80u-collect/repos/nx741j

# 方式 A：试打（不落盘）
git apply --check -p1 xnu/patches/0103-unixnu-native-4k-bootstrap-capacity.patch

# 方式 B：按 hunk 内容比对（忽略头格式差异）
diff <(grep -E '^[+-][^+-]' xnu/patches/0103-*.patch | sort -u) \
     <(grep -E '^[+-][^+-]' ../UniXNU/patches/xnu/native-4k-bootstrap-capacity.patch | sort -u)
```

| 结果 | 判定 | 动作 |
|---|---|---|
| 无输出 / `check` 通过 | 与上游同源 | `device` 序列维持空；设备仓库三份补丁标注 `upstreamed` |
| 有输出 | 含设备特有改动 | 仅把差异部分做成 `device` 层补丁列入 series |

已知背景：设备版是 `diff --git` 格式、主仓库版是 classic `---/+++` 格式，因此**不能直接 diff 两个文件**，
必须先按 hunk 内容或统一导出格式比对。

### 17.2 其余前置（按依赖顺序）

| # | 检查 | 命令 / 依据 | 预期 |
|---|---|---|---|
| 2 | 内核产物存在 | `./unixnu build kernel`，或 `--kernel FILE` 指向已有 `kernel.development.pandora` | `out/<…>/kernel/kernel.macho` 生成 |
| 3 | `[kernel.prelink]` 补齐 | 参考页字段：`tool` / `kernel` / `slide` / `roots` / `early_security_init` / `kernelcache` / `kernelcache_sha256` | `unixnu status --json` 能解析出 prelink 配置 |
| 4 | kernelcache 固定哈希 | 输入 `/opt/work/ipsw/kernelcache.macho`（或 `.arm64`），算 sha256 写入 `kernelcache_sha256` | 预链接可复现 |
| 5 | `[kernel] archives` | 静态库脚本顺序（含 `ArmDTPlatform`、`ArmCpu` 产物 `lib*.a`） | `-force_load` 后内核含两个内置驱动 |
| 6 | 4 KiB 路径三件套 | `ARM_PGSHIFT=12`、`native-4k-user-abi`（commpage/mmap shift 12）、`native-4k-signature-hash-cache`（`csb_native4k_hashes`） | 内核启动后用户态 ABI 为 4K |
| 7 | 高位物理地址 | `zzx-pmap-sync-64bit-pa`（`ptoa()` 支持 >4 GiB DRAM） | 5 GB 内存窗口可用 |
| 8 | 记录更新 | `porting.toml [support].status` 与 `verified_devices` | 与真机结果一致 |

### 17.3 与当前阻塞的关系

以上全部依赖**第 2 层 DXE 链跑通**（现在卡 `RpmhDxe FAR=0x58`）。因此第 5 层开工前还需先完成：

1. `RpmhDxe` 异常定位（`boot-uefi-info.img` 已开 DEBUG_INFO，等 9008）；
2. Clock / PMIC / UFS 依次通过，BDS 起菜单；
3. payload 卷放入可读分区（efisp FAT 或 userdata），交接二第 3 条闭环。


### 17.4 补充：0103 已判定（同源）

按 17.1 的方式 B 实跑（按 hunk 内容比对，忽略补丁头格式）：

```
device +/- 行数: 20      main +/- 行数: 20
diff 结果: exit=0, 0 行差异
文件目标一致: osfmk/arm/pmap/pmap.h, osfmk/arm64/genassym.c, osfmk/arm64/start.s
```

结论：`0103-unixnu-native-4k-bootstrap-capacity.patch` 与主仓库
`native-4k-bootstrap-capacity.patch` **hunk 内容完全相同**，仅 git 头与 classic 头的差异。

因此三份设备补丁（0103/0104/0105）全部为已上游化副本：

| 设备补丁 | 判定 |
|---|---|
| 0103 bootstrap-capacity | ✅ 同源（本轮） |
| 0104 user-abi | ✅ 同源 |
| 0105 signature-hash-cache | ✅ 同源 |

`soc/` 与 `device/` 补丁序列维持为空即为最终状态；「内核行为」一行由 ⚠️ 转为 ✅（机制 + 归属均已确认）。
遗留未完成项只剩**上游能力缺口**：kit 打包（`payload.py` 硬编码 `autoload = false`）、QEMU 验证通道（缺 ArmVirtPkg）、
内核产物（第 5 层）。
---

## 十八、第 2 层 DXE 链攻坚记录（2026-11-06 00:2x–00:5x）

### 18.1 当前链路状态（真机，INFO 开启时）

```
DALSYS ✅ → HWIO ✅ → ChipInfo ✅ → PlatformInfo ✅
→ HALIOMMU ⚠️ Not Ready（缺 DTB extension）
→ ULog ✅ → IPCC ✅ → CmdDb ✅ → PwrUtils ✅ → Npa ✅
→ RpmhDxe ❌ 入口 +0x10A4 崩溃（FAR=0x58）
→ MailboxDxe ⚠️ Failed to locate SCM protocol
→ ShmBridge ✅ → TzDxeLA ❌ TzeLoaderDxe.c +1158 断言 → 复位
```

### 18.2 RpmhDxe 崩溃精确定位

| 项 | 值 |
|---|---|
| 镜像基址 | `0xC1680000` |
| 入口 | `0xC1690000`（= base + 0x1000，与 PE `AddressOfEntryPoint` 一致） |
| ELR | `0xC16900A4` → **RVA 0x10A4** |
| 故障指令 | `10A4: ldr x3, [x2]`，其中 `x2 = gBS + 0xC0`（前序 `109C: ldr x2,[x3]`、`10A0: add x2,x2,#0xc0`） |
| FAR | `0x58`（与 `gBS+0xC0` 不自洽，说明取到的是异常指针） |

### 18.3 入口 ABI 已排除

`KernelStubDxe` 增加运行时探针（ERROR 级）：

```
KernelStub: ImageHandle=0xC1920D18 ST=0x10C50B18 gBS=0xC2120000 ST->BootServices=0xC2120000
SmemStubDxe: entry (gBS=0xC2120000)
```

`gBS` 与 `SystemTable->BootServices` **完全一致**，DxeCore 传给厂商驱动的 SystemTable 正常 → **不是 ABI 不匹配**。

### 18.4 顺序实验（均无效）

| 实验 | 结果 |
|---|---|
| 摘除 RpmhDxe | 链继续，但 `ClockDxe` 报 `ClockTargetLib.c +425` 断言 → 复位循环 |
| ShmBridge/ScmDxeCompat/TzDxeLA 提到 Mailbox 之前 | TzDxeLA 断言依旧，且更早暴露 |
| 仅把 QcomScmiDxe 移到 TzDxeLA 之后 | `QcomScmiDxe` 的 PC alignment fault 消失，但 TzDxeLA 断言仍在 |
| 摘除 TzDxeLA | 断言消失，但链路不再前进（无可见输出） |

### 18.5 零可见性陷阱（重要）

关闭 `PcdDebugPrintErrorLevel` 的 INFO 位后：

- `FrameBufferSerialPortLib` 在 SEC/PEI 阶段**没有可显示的输出**（显示路径在 DXE 后期才就绪）；
- 唯一可见的是 `KernelStub` / `SmemStub` 的 ERROR 行；
- clean/landmark 镜像屏幕**停留在旧画面**（framebuffer 不会被清空），无法据此判断是否在跑。

结论：**排查阶段不要关 INFO**；要关的话必须保留至少一个 DXE 中段的 ERROR 地标。

### 18.6 根因收敛

四个卡点是同一根因的不同侧面——**BootShim 直跳丢失了 ABL 环境**：

| 缺失项 | 正常提供者 | 后果 |
|---|---|---|
| SMEM 内容（全 0） | XBL 初始化 | ChipInfo/Rpmh 等静态链接 smem 库的驱动失败 |
| DTB extension（336 字节结构） | XBL 经 HOB 传入 | HALIOMMU `Not Ready`、Mailbox `Locate DTB Failed` |
| TZ 应用镜像（tzapps） | **ABL 加载 tzapps 分区** | TzDxeLA `TzeLoaderDxe.c +1158` 断言 |
| RPMh 通道配置 | ABL/XBL 交接 | RpmhDxe 入口崩溃、ClockDxe 断言 |

继续打桩只能逐点绕过，无法补齐 ABL 环境本身。

### 18.7 决策：换根（改为 ABL 正常加载路径）

放弃"BootShim 直跳 + 补桩"，改为把我们的 FV 以原厂 `uefi_a` 的封装方式写进固件分区，让 **ABL 按正常流程初始化**（SMEM / HOB / tzapps），从而：

- 删除全部桩驱动（SmemStubDxe、KernelStubDxe、Scheduler HOB、RPMh 映射补丁）；
- 厂商驱动链恢复设计预期。

执行计划：

1. 解析原厂 `uefi_a.img`（5,238,784 B）的封装：ELF 头 / 入口 / FV 偏移 / 校验字段；
2. 把 `SILICIUM_UEFI.fd` 按同格式封装；
3. 刷 `uefi_a`（LBA 6），`boot_a` 保留为回退；
4. 失败时（历史上有过黑屏记录）逐项比对 SEC 入口约定、FD base、header 校验与分区长度。
