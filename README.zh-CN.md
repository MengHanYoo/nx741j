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

## 当前进度（2026-11-20）

本节取代原先跟在许可之后的按日期日志，只写在本机实测过的东西。

### 启动链（官方移植阶段 1-5）

| 阶段 | 状态 | 证据 |
|---|---|---|
| 1 启动镜像 | 完成 | efisp loader 从 ABL 接管（本板替代 BootShim，偏差记录在 docs/） |
| 2 UEFI 固件 | 完成 | Mu 固件卷位于 rawdump + 64 MiB、DXE 日志、ConIn 修复 |
| 3 启动管理器 | 完成 | UniXNU 菜单、配置与 BootEntry |
| 4 XNU 加载器 | 完成 | DarwinBoot MARK #29、EBS ok |
| 5 内核 | 完成 | 平台专家、IOKit、kext 加载、控制台 |

### UFS（官方 porting/ufs 完成条件）

| 条件 | 状态 | 证据 |
|---|---|---|
| 1 寄存器 | 完成 | CAP=0xd5c7031f、HCS=0x10f、HCE=1 |
| 2 DMA / 队列所有权 | 完成 | 队列已安装并启动（rsr=1）；控制器只取固件队列区 |
| 3 命令完成 | 完成 | TEST UNIT READY lun0: ocs=0 resp=00 scsi=00 db=0 |
| 4 已知块读取 | 完成 | READ(10) LBA 1 返回 sig=4546492050415254（EFI PART）、entlba=2 cnt=32 esz=128；分区表解析出 userdata：first=0x4a6328 |
| 5 根卷 | 部分 | 经内核自身传输读到官方 SHNGROOT 清单，策略校验通过（img=0x1f3800000、w0=0x14fe08600、w1=0xa3800000、bounds=1 ord=1）；OS 窗口第 4 块返回 NXSB；把窗口发布成 IOMedia 是下一步 |
| 6 持久化、7 性能 | 未开始 | |

冷启动（native-ufs-cold-init=1）完整走厂商序列：PHY 校准、SERDES 启动、PCS 就绪（0x400 00000001）、HCE、DME_LINKSTARTUP（HCS=0x10f、UPMCRS=1）。用 data-a-live pandora_ufs_clocks() 的 SM8850 偏移读取时 UFS 的 GCC 分支全部为使能；流 0x60 被强制匹配并旁路（SMR[0]=0x80000060、S2CR[0]=1），所有等待改为有界忙等——本内核上 IODelay 会阻塞在定时器上。

### 本机已验证的存储工作

* 官方 userdata 镜像已构建（7.8 GiB，SHNGROOT v1，OS 5,635,081,728 B @ 0x100000，SystemOS 2,743,074,816 B @ 0x150000000），写入 userdata（LBA 4,875,048）并回读逐字节一致；
* nxRootRead 每条命令经固件队列发一个 4 KiB READ(10)，按物理地址维护缓存并拷入调用者缓冲区；块设备经它读取，探针读 OS 窗口返回 APFS 容器超级块 NXSB；
* data-a-live 的 pinfile 在本机可用：64 MiB 与 16 GiB 钉住文件、extents（1 段与 11 段），以及经 /dev/block/sda12 的 rawtest/rawcheck，即 dm-default-key 层可像参考机一样绕过；
* 内核窗口已覆盖固件队列：DarwinBoot 优先选包含 0xC3B00000 的空闲段，QEMU 下回退，并把覆盖范围上报到 0xD0000000，arm_vm_init 不再拒绝这些物理地址。

### 驱动补丁与构建状态

* patches/drivers/native-ufs-adopt-root.patch 是可审查的 adopt-root 改动（块读、清单布局、startRoot 优先、probe=12 汇报）；
* scripts/patch-native-ufs-cold-init.py 负责应用它，未接入 board.toml——因为 ./unixnu tree 会在生成树时执行 board 钩子，接上后补丁会被打两次、第二次必然被拒。摘除钩子后树可干净构建（kernel_rc=0、无编译错误）；
* 两条安全的收尾方式见 docs/STATUS-2026-11-19.md 第 10 节：恢复原先幂等的字符串补丁脚本并把统一 diff 作为记录，或从锁定的上游版本在临时目录里重新生成 diff。

### 未决问题与下一步

1. 在设备上跑 probe=12，期望 publication done=1 published=2（OS 与 SystemOS 成为块设备）。
2. 补 /chosen/root-matching（OS 卷 UUID），让 XNU 选中该窗口作为根。
3. 生产 hostBringUp 路径（自建 DMA + 完整 SMMU 窗口）在本板不可用；adopt 传输是可行路线，细节见 docs/STATUS-2026-11-19.md。
4. 用户态前的 ADT 缺口：外设 unixnu,adt-name、/chosen 的 osenvironment/crypto-hash-method/root-matching、memory-map 占位符。
5. panic 文本仍只能拍屏；console 缓冲只覆盖普通打印。