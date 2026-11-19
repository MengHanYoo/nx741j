# UniXNU-nx741j

[English](README.md) | **简体中文**

**努比亚 Z80 Ultra（NX741J）** 的设备仓库 — 高通 **SM8850**（Kaanapali，代号 `canoe`），8 核，
Adreno 840，UFS，面板 `bf375`/`rm692h5` 1440×3200。

本移植通过官方 UniXNU 链路启动 Apple XNU：

```
bootloader -> efisp loader -> UniXNU (UEFI) -> DarwinBoot -> XNU -> userland
```

目标系统：**iOS 16.7.16（20H392）**，XNU **8796.141.3**，arm64，4 KiB 页。OS profile 固定在
[board.toml](board.toml) 中，是本移植唯一可用的 IPSW。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| 1 | 板级描述、组件、构建输入 | 完成 |
| 2 | 固件（UniXNU UEFI 应用、DarwinBoot） | 完成 |
| 3 | 负载（内核、ADT、trust cache） | 完成 |
| 4 | 启动链交接（loader -> UEFI -> DarwinBoot -> XNU） | 完成 |
| 5 | 内核 bring-up（platform expert、IOKit、预链接 kext） | **完成** |
| 6 | 存储（UFS） | **阻塞 — 平台（XBL/TZ/xPU）** |
| 7 | 用户态（根卷、`launchd`） | 被阶段 6 阻塞 |

**阶段 5 里程碑（2026-11-12）**：内核 console 已能直接画到面板上，所有启动阶段都可以在设备上
观察，不再只有彩带屏：

```
Registered class IOResources.
...
Starting from prelinked kernel.
kext com.apple.kpi.bsd Loaded.
...
loading dependencies com.apple.kpi.mach
```

**阶段 6（UFS）**：`NativeUfs` 已匹配 `/arm-io/ufshc`、完成控制器接管并装好自己的请求列表。
但门铃写入仍会挂起总线：控制器只在 bootloader 留下的状态下接受命令，而该状态无法承受主机控制器
复位，因为本内核尚未初始化 UFS PHY/时钟栈。已实现厂商还原的冷启动路径
（`native-ufs-cold-init=1`：PHY 校准表、SERDES 启动、PCS ready、HCE、DME 链路启动），等待真机验证。

## 构建

两个入口，均在 Linux/WSL 上运行：

```sh
# 快速迭代：kernel -> payload -> firmware，不跑 QEMU 冒烟
./scripts/build-fast.sh ~/nx741j-build/UniXNU

# 里程碑闸门：同样三步，外加一次 QEMU 启动链验证
# 凡涉及固件内存布局的改动，刷机前必须走这一条
sudo ./scripts/build-and-test.sh ~/nx741j-build/UniXNU
```

闸门断言三个标记 —— `UNIXNU: BootEntry`、`MARK #29`、`QEMU: EBS ok, entering the kernel` ——
任一缺失即构建失败。

## 刷机

本机 ABL 拒绝 `fastboot boot`、`fastboot flash`、`fastboot erase`（回 `unknown command`），
因此一切通过 **9008** 与 EFI 系统分区完成：

| 区域 | LUN | LBA | 大小 |
|---|---|---|---|
| `efisp`（我们的 loader） | 4 | 748894 | 3 MiB |
| `rawdump` — 固件 FD | 0 | 4760360 | 4 MiB |
| `rawdump` — payload | 0 | 4743976 | 64 MiB |

```powershell
wsl\flash-boota-9008.ps1 -Img <image> -StartSector <lba> -Label <label> -Lun <lun> -PartitionBytes <bytes>
```

任何一次进 Android 都会覆盖 `efisp`，所以每次测 payload 前都要重刷。写回官方 `efisp`
（`sha256 4df18359...`）即可恢复厂商启动链。

## 仓库结构

| 路径 | 内容 |
|---|---|
| `board.toml` | 板级描述：组件、内核归档、预链接根集、启动参数 |
| `dts/` | 负载用的 ADT 源 |
| `loader/` | efisp EFI loader（`p5.c` ... `p7.c`）及其镜像 |
| `package/firmware-overlay/` | 板级固件代码，构建前复制进 UEFI 树 |
| `patches/xnu/` | 设备 XNU 补丁序列（应用顺序见 `series`） |
| `patches/firmware/` | 固件补丁（DSC/FDF、平台驱动） |
| `scripts/` | 构建脚本（`build-fast.sh`、`build-and-test.sh` 等） |
| `docs/` | 分析、审计、阶段记录、硬件清单 |

## 近期关键修复

| 补丁 / 改动 | 原因 |
|---|---|
| `0010-console-acquire-screen.patch` | `PE_create_console` 改用 `kPEAcquireScreen`，`kprintf` 才能上屏 |
| `0015-iodevicetree-null-interrupt-parent.patch` | `IODTMapOneInterrupt` 把 NULL 父节点传给 `IODTGetICellCounts`，在 `InitIOKit` 崩溃 |
| `component:GopFramebuffer` 进 `[kernel] archives` | 显示组件此前被选中但从未链接 |
| `native-ufs-cold-init=1` | 厂商还原的 PHY 校准 + 链路启动，在任何命令之前执行 |

## 文档

- [docs/TASKS.md](docs/TASKS.md) — 任务与状态汇总
- [docs/STATUS-2026-11-11.md](docs/STATUS-2026-11-11.md) — 逐阶段状态
- [docs/BOOT-CHAIN.md](docs/BOOT-CHAIN.md) — 与官方启动链描述的对照
- [docs/UFS-QCOM-PHY-PORT.md](docs/UFS-QCOM-PHY-PORT.md) — 还原的 PHY 表与调用顺序
- [docs/HARDWARE-INVENTORY.md](docs/HARDWARE-INVENTORY.md) — 硬件事实与来源

## 许可

补丁与板级代码沿用上游 UniXNU 的许可（各文件内标注 BSD-2-Clause-Patent）。厂商还原表为从本机固件
中恢复的数据。

## 任务、代办与未决问题（2026-11-18）

以下每一条都在本机实测过，证据见 [docs/STATUS-2026-11-18.md](docs/STATUS-2026-11-18.md) 及此前的状态文档。

### 已完成

| 事项 | 证据 |
|---|---|
| 阶段 1–5：板级、固件、负载、交接、内核 bring-up | [docs/STATUS-2026-11-12.md](docs/STATUS-2026-11-12.md) |
| console 日志通道（内核 → DRAM → rawdump → 主机） | `patches/xnu/0021`、`loader/p7.c`、`-ReadTo` |
| UFS 寄存器访问、队列所有权、UTRLBA/RSR 回读 | [docs/STATUS-2026-11-15.md](docs/STATUS-2026-11-15.md) |
| 从内核侧补上流 0x60 的匹配 + 旁路（SMR[0]=0x80000060，S2CR[0]=1） | 2026-11-18 第 1 节 |
| DME_GET / DME_SET（PA_PWRMode SLOW → FAST） | 第 2 节 |
| sweet_ufs 移植及其仅依赖 libkern 的构建 | `patches/xnu/0022` |
| 独立 loader 移植 + Mach-O → SWEETXNU 负载转换器 | `bootshim/`、`scripts/macho-to-sweet-payload.py` |

### 阻塞中

**阶段 6 — 本机无法由 HLOS 驱动 UFS。** 主机始终不取 UTRD（DBR 一直为 1），
无论列表放在我们的 DMA 还是引导程序自己的区域，而 UTRLRSR 读回 1、HCS=0x10f。
所有软件侧可能原因都已实测并排除：

* 固件：移除 UfsPassThruDxe 后行为不变（第 9 节）；
* 仅支持 MCQ：CAP bit29 = 0，legacy 单门铃是支持的（第 4 节）；
* 描述符编码：与 `ufshci.h` 逐位一致（第 4 节）；
* 控制器复位：HCE 循环后主机不再应答（第 3 节）；
* SMMU context bank：XNU、固件、TZ_IO_ACCESS_READ 三条路都被拒绝（第 5、7 节）；
* 引导程序没有执行 SMR0 交接：DT 声明 `qcom,handoff-smrs = <0x800 0x02>`（SMR0 由引导程序配置，
  与成功的 data-a-live 机一致），但实测 `SMR[0]=0x00000060`（VALID=0）、`S2CR[0]` 为 fault；
* 引导程序配置：`xbl_config.img` 是签名 MBN，UFS 键只是 schema 表；
  `NX741_unlock/` 全量扫描无该键（第 10 节）。

**阶段 7 — 用户态** 仍被阶段 6 阻塞。

### 下一步（每一项都需要我们目前没有的东西）

1. **厂商签名的 XBL/TZ 改动**，让流 0x60 对非安全世界保持匹配 + 旁路，或提供内核可编程的 stage-1 上下文；
2. **固件侧挂载根卷**：需要 Mu 在 `ExitBootServices` 之后继续服务块请求。Mu 是单核（`Mu_Basecore` 无 MpServices/PSCI），
   因此要先做 SMP 移植、共享环协议，再写 XNU 侧块驱动；
3. **独立 EFI loader**：本机 `fastboot boot` 要求 kernel 字段是 PE（`No EFI image found in boot.img`），
   所以 loader 要 PE 化，负载放进 ramdisk 字段（第 11 节）。

### 待办清单

| 事项 | 说明 |
|---|---|
| ADT：外设节点补 `unixnu,adt-name` | 阶段 7 需要匹配 |
| ADT：`/chosen` 的 osenvironment、crypto-hash-method、root-matching | 目前缺失 |
| ADT：`memory-map` 占位符 | 目前为空 |
| 把 panic 文本写进日志通道 | `panic()` 输出未被捕获，只有屏幕可见 |
| OS 侧按键（ButtonProbe，report 2） | DXE 触碰 TLMM 会 assert |
| `0008-granular-protection-noop.patch` 不在 `series` 中 | `board.toml` 注释仍引用它 |
| 记录 `early_security_init = ""`（AMFI/Image4）偏离 | 阶段 7 前必须处理 |
| trust cache / unique_chip_id 占位 | 自制扁平包；发布前需说明 |
| bootshim 构建产物入库 | 自 719722b 起由 `.gitignore` 排除 |

### 未决问题

* 是谁把 `SMR[0].VALID` 清掉的——XBL、ABL 还是 TZ？只读遍历运行在固件之后，写入者尚未定位；
* `xbl_config` 里 `UfsSmmuConfigForOtherBootDev` 的编译值是多少？
* 如果 CB 窗口被打开，TZ 是否接受 HLOS 自建的 stage-1 上下文？

### 复现证据

```
wsl bash _tmp/t2/noaes2_2017.sh      # 带探针补丁的内核
wsl bash _tmp/t2/pay_step2b_2017.sh  # 负载（boot-args 取自 board.toml）
wsl bash _tmp/t2/qemu_step2_2017.sh  # 闸门：EBS ok
wsl bash _tmp/t2/fw_2017.sh          # 固件（SILICIUM_UEFI.fd）
```

上机：9008（`wsl/flash-boota-9008.ps1`）刷入 → 启动一次 → 热重启 → 再进 9008 → `-ReadTo` 读日志。

## 2026-11-19 更新 — UFS 命令已能完成，卡在数据阶段

### 上次快照之后完成的事

| 事项 | 证据 |
|---|---|
| 冷启动走完厂商序列（PHY、SERDES、PCS、HCE、DME_LINKSTARTUP） | `PCS ready (0x400 00000001)`、`HCS=0x10f`、`UPMCRS=1` |
| UFS 的 GCC 分支全部开启（用 data-a-live 的 SM8850 偏移） | `axi/ahb/unipro/aggre/ice` 使能位全为 1 |
| 本机第一条真正完成的 UFS 命令 | `TEST UNIT READY lun0: ocs=0 resp=00 scsi=00 db=0` |
| 主机只取固件队列区里的请求 | 自建 DMA（`0xb4a09000`）→ `dbr=1`；固件区 → 命令完成 |
| 等待有界，不再卡定时器 | 依官方 UFS 页，把 6 处 `IODelay` 换成忙等 |

### 待办与未决问题（按顺序）

1. **数据阶段写不回可见数据**：`READ CAPACITY` 完成但 8 字节为 0（`unexpected LUN0 block size 00000000`）。
   adopt 路径原来按虚拟地址清 cache，现改为按物理地址刷（与 data-a-live 一致），并让 probe=9 在 panic 前扫描
   16 KiB 区域找期望的容量模式；结果待测。
2. **内核物理窗口不含固件队列区**：`arm_vm_init` 拒绝 `0xc3b04000`，因为 DarwinBoot 只给了 256 MiB 窗口
   （`0xb03c8000 + 0x10000000`）。`WINDOW_SIZE` 已改为 512 MiB 并重建固件，下次启动验证分配是否覆盖该区。
3. **阶段 7 无法开始**：必须先能稳定读取已知块（问题 1）并定位根卷。
4. **阶段 7 前的 ADT 缺口**：外设 `unixnu,adt-name`、`/chosen` 的 `osenvironment`/`crypto-hash-method`/`root-matching`、
   `memory-map` 占位符。
5. **panic 文本不进日志通道**，目前只能拍屏。
6. **厂商侧**：DT 声明 `qcom,handoff-smrs = <0x800 0x02>`，但引导程序没有执行；开关在签名 `xbl_config` MBN 里，
   只有厂商签名的 XBL/TZ 改动才能变——这就是本移植不能依赖 HLOS 拥有 SMMU context 的原因。

### 复现

```
wsl bash _tmp/t2/noaes2_2017.sh      # 内核（patch-native-ufs-cold-init.py 负责探针）
wsl bash _tmp/t2/pay_step2b_2017.sh  # 负载（boot-args 取自 board.toml）
wsl bash _tmp/t2/qemu_step2_2017.sh  # 闸门：EBS ok
wsl bash _tmp/t2/fw_2017.sh          # 固件
```

### 2026-11-19 深夜追加（首版 README 归档之后）

| 事项 | 状态 |
|---|---|
| UFS 数据阶段 | 未决：`READ CAPACITY` 完成但读回 8 字节为 0（`blockSize=00000000`）；adopt 路径已改为按物理地址刷缓存，probe=9 在 panic 前扫描 16 KiB 区域找期望模式 |
| 内核物理窗口 | 未决：`arm_vm_init` 拒绝固件队列区（`illegal PA 0xc3b0_9000`），因为 boot_args 只带 256 MiB；ADT 已加 `darwinboot-memory-size = 0x20000000`，DarwinBoot 强制 `WindowSize >= SIZE_512MB`，固件已重建，待验证 |
| 固件重建 | 构建实际编译的是 `UniXNU/drivers/DarwinBoot`（不是设备仓库副本）；两份副本现已同步这两处改动 |
| 设备状态 | 已恢复：原厂 `efisp`、原厂 `boot_a`/`boot_b`、`misc` 已清，Android 可启动 |
| 下次开工 | 刷 payload `2ca4c0e2` + 固件 `8b835dff`，确认 `size 0x20000000` 且无 `illegal PA`，然后读 GPT 头 |

## 当前归档 — 2026-11-19（UFS 里程碑）

### 存储状态

| 官方 UFS 完成条件（porting/ufs） | 状态 | 证据 |
|---|---|---|
| 1 寄存器 | 完成 | `CAP=0xd5c7031f`、`HCS=0x10f`、`HCE=1` |
| 2 DMA / 队列所有权 | 完成 | 队列安装并启动（`rsr=1`），请求只从固件队列区被取走 |
| 3 命令完成 | **完成** | `TEST UNIT READY lun0: ocs=0 resp=00 scsi=00 db=0 is=5` |
| 4 已知块读取 | **完成** | `READ(10)` LBA 1 → `sig=4546492050415254`（EFI PART）、`entlba=2 cnt=32 esz=128` |
| 5 根卷 | 下一步 | 已定位 userdata，尚未读其首块 |
| 6 持久化、7 性能 | 未开始 | |

冷启动（`native-ufs-cold-init=1`）完整走完厂商序列：PHY 校准、SERDES 启动、PCS 就绪
（`0x400 00000001`）、HCE、`DME_LINKSTARTUP`（`HCS=0x10f`、`UPMCRS=1`）。用 data-a-live
`pandora_ufs_clocks()` 的 SM8850 偏移读取时，UFS 的 GCC 分支全部为使能状态。

流被强制匹配并旁路（`SMR[0]=0x80000060`、`S2CR[0]=1`），等待改为有界忙等（官方页明确
`IODelay` 会阻塞在定时器上），请求描述符显式构造（`u7=0x01000001`、`prd0=phys+6144`、`prd3=7`）。

本机实测 GPT 数据：

```
hdr=1 ents=1 entlba=2 cnt=32 used=12 userdata=11 first=4a6328 last=ed4f7fa n0=ssd
```

`first=0x4a6328` = 4,875,048，与厂商 `rawprogram0.xml` 里 `userdata` 的 `start_sector` 一致；
首个分区名解码为 `ssd`。

### 待办与未决问题

1. **根卷**：读 userdata 首块（LBA 4,875,048），确认文件系统并定位 iOS 根卷窗口
   （官方页的 RootLayout / 根 IOMedia）。
2. **内核窗口与固件队列**：DarwinBoot 现在优先选择包含 `0xC3B00000` 的空闲段，QEMU 下回退，
   并把上报覆盖范围扩到 `0xD0000000`；改任何内存常量前先看这条。
3. **请求描述符目前在探针里内联构造**；驱动侧 `adoptAndRead` 的 lambda 在生产使用前需要做同样的
   字段核对。
4. **厂商交接**：DT 声明 `qcom,handoff-smrs = <0x800 0x02>`，但引导程序没有执行；开关在签名
   `xbl_config` MBN 里。
5. **阶段 7 前置**：ADT 外设 `unixnu,adt-name`、`/chosen` 的 `osenvironment`/`crypto-hash-method`/
   `root-matching`、`memory-map` 占位符。
6. **可观测性**：panic 文本仍只能拍屏，console 缓冲只覆盖普通打印。
