# UniXNU-nx741j

[English](README.md) | **简体中文**

**努比亚 Z80 Ultra（NX741J）** 的设备仓库 — 高通 **SM8850**（Kaanapali，代号 `canoe`），8 核，
Adreno 840，UFS，面板 `bf375`/`rm692h5` 1440×3200。

本机最终采用的 NX741J iOS 路线是：

```
bootloader -> boot_a shim loader
  -> SWEETXNU payload -> XNU -> HFSX ufsroot0 -> launchd
```

另有独立的 `efisp -> rawdump -> Mu UEFI -> DarwinBoot` 入口；仓库仍保留其
实现和诊断，但它不是最终真机镜像，本机 Mu 停在 DxeCore/BDS 句柄枚举。
两条入口不能混用。已验证路线、哈希和 9008 回读证据见
[PROVEN-ROUTE-2026-11-23.md](docs/PROVEN-ROUTE-2026-11-23.md)。

目标系统：**iOS 16.7.16（20H392）**，XNU **8796.141.3**，arm64，4 KiB 页。OS profile 固定在
[board.toml](board.toml) 中，是本移植唯一可用的 IPSW。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| 1 | 板级描述、组件、构建输入 | 完成 |
| 2 | 固件（UniXNU UEFI 应用、DarwinBoot） | 完成 |
| 3 | 负载（内核、ADT、trust cache） | 完成 |
| 4 | 启动链交接 | 已验证的 loader -> SWEETXNU -> XNU 镜像已写入并回读 |
| 5 | 内核 bring-up | 已验证内核在 shim-loader QEMU 中进入中断控制器；历史真机已到用户态 |
| 6 | 存储与驱动 | 39 段固定 extent HFSX `ufsroot0`；显示/触摸/HID/CPU 代码在成功内核中 |
| 7 | USB/Wi-Fi/音频/基带/相机/背屏 | 按你的要求冻结，等 iOS 用户态可用后再做 |

**2026-11-23 检查点**：根卷来自原始 IPSW，不使用 `iosroot-GOLDEN.img`。
本次在一次 9008 会话中写入 13 个根差异块、原厂 efisp 和 54,329,344 字节
`boot_a`，全部逐区回读一致；复位后旧的 `QUSB_BULK` 失败形态未再出现。

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

**阶段 6（UFS）**：存储路径直接建立在引导程序留下的固件状态上（官方 porting/ufs 条件 2）：
请求列表沿用固件的 UTRLBA，流 0x60 被强制匹配并旁路（`SMR[0]=0x80000060`、`S2CR[0]=1`），
厂商冷启动序列（PHY 校准、SERDES 启动、PCS 就绪、HCE、DME 链路启动）在
`native-ufs-cold-init=1` 之后执行。所有等待改为有界忙等——本内核上 `IODelay` 会阻塞在定时器上。

| 条件 | 状态 | 证据 |
|---|---|---|
| 1 寄存器 | 完成 | `CAP=0xd5c7031f`、`HCS=0x10f`、`HCE=1` |
| 2 DMA / 队列所有权 | 完成 | 队列已安装并启动（`rsr=1`）；控制器只取固件队列区 |
| 3 命令完成 | 完成 | `TEST UNIT READY lun0: ocs=0 resp=00 scsi=00 db=0` |
| 4 已知块读取 | 完成 | `READ(10)` LBA 1 返回 `sig=4546492050415254`（EFI PART）、`entlba=2 cnt=32 esz=128`；解析出 `userdata`：`first=0x4a6328` |
| 5 根卷 | 部分 | 经内核传输读到官方 `SHNGROOT` 清单，`img=0x1f3800000`、`w0=0x14fe08600`、`w1=0xa3800000`、`bounds=1 ord=1`；OS 窗口第 4 块返回 `NXSB`；把窗口发布成 `IOMedia` 是下一步 |
| 6 持久化、7 性能 | 未开始 | |

官方 `./unixnu userdata` 镜像（7.8 GiB，`SHNGROOT` v1，OS 5,635,081,728 B @ `0x100000`，
SystemOS 2,743,074,816 B @ `0x150000000`）已写入 `userdata`（LBA 4,875,048）并回读逐字节一致。
`nxRootRead` 每条命令经固件队列发一个 4 KiB `READ(10)`，块设备与根卷探针因此能读到真实文件系统
数据；data-a-live 的 `pinfile` 在本机同样可用（64 MiB 与 16 GiB 钉住文件、1 段与 11 段 extents、
经 `/dev/block/sda12` 的 `rawtest`/`rawcheck`），即 dm-default-key 层可像参考机一样绕过。内核窗口
已覆盖固件队列：DarwinBoot 优先选包含 `0xC3B00000` 的空闲段，QEMU 下回退，并上报到 `0xD0000000`。

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

同时生成 normal-root 与旧版 `probe=12` 对照 payload：

```sh
./scripts/build-payload-modes.sh ~/nx741j-build/UniXNU
```

产物在 `out/payload-modes/`；脚本结束前会恢复 tree 中的 normal-root payload。

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
| `patches/drivers/` | 设备驱动 diff（adopt 传输根卷工作） |
| `scripts/` | 构建脚本（`build-fast.sh`、`build-and-test.sh` 等） |
| `scripts/patch-native-ufs-*.py` | ShengUfs 预置补丁与驱动 diff 的构建钩子 |
| `docs/` | 分析、审计、阶段记录、硬件清单 |

当前结论、根卷修复、QEMU 边界和 9008 回读报告以
[docs/PROVEN-ROUTE-2026-11-23.md](docs/PROVEN-ROUTE-2026-11-23.md) 为准。

## 近期关键修复

| 补丁 / 改动 | 原因 |
|---|---|
| `0010-console-acquire-screen.patch` | `PE_create_console` 改用 `kPEAcquireScreen`，`kprintf` 才能上屏 |
| `0015-iodevicetree-null-interrupt-parent.patch` | `IODTMapOneInterrupt` 把 NULL 父节点传给 `IODTGetICellCounts`，在 `InitIOKit` 崩溃 |
| `component:GopFramebuffer` 进 `[kernel] archives` | 显示组件此前被选中但从未链接 |
| `native-ufs-cold-init=1` | 厂商还原的 PHY 校准 + 链路启动，在任何命令之前执行 |
| `patches/drivers/native-ufs-adopt-root.patch` | adopt 传输块读、清单布局、`startRoot` 优先、`probe=12` 汇报 |
| 有界忙等 | 本内核上 `IODelay` 会阻塞在定时器上，所有 UFS 等待改为带截止的忙等 |

## 文档

- [docs/TASKS.md](docs/TASKS.md) — 任务与状态汇总
- [docs/INTEGRATION-2026-11-22.md](docs/INTEGRATION-2026-11-22.md) — 框架补丁与可复现整合记录
- [docs/STATUS-2026-11-22.md](docs/STATUS-2026-11-22.md) — 当前验收边界与剩余任务
- [docs/DEVICE-TEST-2026-11-22.md](docs/DEVICE-TEST-2026-11-22.md) — 真机 9008 写入与复位失败记录
- [docs/STATUS-2026-11-11.md](docs/STATUS-2026-11-11.md) — 逐阶段状态
- [docs/BOOT-CHAIN.md](docs/BOOT-CHAIN.md) — 与官方启动链描述的对照
- [docs/UFS-QCOM-PHY-PORT.md](docs/UFS-QCOM-PHY-PORT.md) — 还原的 PHY 表与调用顺序
- [docs/HARDWARE-INVENTORY.md](docs/HARDWARE-INVENTORY.md) — 硬件事实与来源

## 许可

补丁与板级代码沿用上游 UniXNU 的许可（各文件内标注 BSD-2-Clause-Patent）。厂商还原表为从本机固件
中恢复的数据。

## 当前进度 — 2026-11-22

阶段 6 的动态根锚点和 VerifiedRoot 已完成主机验证：根镜像不再使用固定
`nxroot-lba`，尾部 `NXROOTP1` 锚点严格校验 GPT 边界、manifest LBA、镜像长度和
SHA-256；OS 1,375,753 块与 SystemOS 669,696 块均由 C/RSA 实现逐块认证。

正常根模式已接入默认 payload：

```text
keepsyms=1 kextlog=0x1 sweetfb_hz=60 sweetfb_noconsole=1
native-source-storage=1 native-ufs-root=1 sheng-pmgr=2
native-ufs-diagnostics=0 native-ufs-dma-offset=0
native-ufs-cold-init=1 native-ufs-keepqueue=2
```

PMGR 设备构建器通过 340 项 provider 虚表审计，最终内核包含
`_sheng_pmgr_run`；但报告仍标记 `runtime_qualified: false`，不能替代真机电源验证。
正常根 payload 和内嵌 `Apple_APFS` content hint 已通过构建器检查。

最终主机产物：

| 文件 | SHA-256 |
|---|---|
| `kernel.macho` | `255cdf323d1d00bc5e077a628d9f6687c6f354de7dc2d9739f2923a70dbd3c9b` |
| `payload.img` | `8801fbd728c35227c193d2aac8dfcf1302ea02a5b2f47c55b07c19f9ab0de95c` |
| `payload-probe12.img` | `fa777aeb86eab4b60c573b3086d2b309ffeed1972f00b4a91e18c34337cf0036` |
| `SILICIUM_UEFI.fd` | `3ce5c6d2c61891975c52846358bff3f3d9744413d61fd732ea48f7507a49a009` |
| `boot.img` | `dbb6c35c9e133c35fe3b7c2d52a4b129239db68ca3d3018309acce7aa10e004d` |

QEMU 在 16 GiB、`1 socket × 8 cores × 2 threads`（16 vCPU）下通过
`UNIXNU: BootEntry`、`MARK #29` 和
`QEMU: EBS ok, entering the kernel`，panic=0。真机还没有 APFS 选卷、
`launchd`、显示、亮度、触摸或按键证据。
第 7 步及持久 Data/性能按用户要求冻结到能启动 iOS 之后。

随后设备实际接入 9008：根镜像、`NXROOTP1` 锚点、payload、FD 和旧
`efisp-p7.img` 都已写入并回读一致。再次进入 EDL 后取到旧 loader 捕获的
内核日志：XNU 已完成 early VM 和 kext collection，并打印
`NATIVE_UFS start`、`NATIVE_UFS bringup: enter` 与 `hc=...`。因此问题不是
loader 没跑，而是旧正常根路径随后误入 `hostBringUp()`；该路径已改为
`cold init → nxAdoptBind → startRoot`，失败会明确报因并停止。

复位后设备仍枚举为 `QUSB_BULK_CID:046A_SN:098F0FC3`，还不是 iOS 启动证据。
另外确认旧 loader 因 `gRawBase` 未赋值，把进度记录误写到 `super` LBA
`20224`；源码已修复并以 rawdump 基址 fail-closed，新 loader SHA-256 为
`73b56bf2b2192706ec724974f832457d5b95ab1382038e3442bc392127b4b246`。
修复后的 loader 和最终 probe payload `fa777aeb…` 已于 13:32 写入手机并回读一致。
`super` LBA `20224` 的实际内容与 2026-11-09 备份完全一致，因此没有执行写回。
下次进入 9008 时按 [DEVICE-TEST-2026-11-22.md](docs/DEVICE-TEST-2026-11-22.md)
读取新的 progress、NXLOG 和 UFS snapshot。

完整的 UniXNU 框架差异在 `patches/framework/UniXNU-ba9e14fb-nx741j.patch`，应用与复现方法见
[docs/INTEGRATION-2026-11-22.md](docs/INTEGRATION-2026-11-22.md)，当前验收边界见
[docs/STATUS-2026-11-22.md](docs/STATUS-2026-11-22.md) 和
[docs/OPEN-ITEMS.md](docs/OPEN-ITEMS.md)。
