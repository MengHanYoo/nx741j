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
| 6 | 存储（UFS） | **进行中** |
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
