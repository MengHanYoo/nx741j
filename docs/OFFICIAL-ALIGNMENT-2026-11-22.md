# 官方路线对齐审计 — 2026-11-22

本文件记录一次纠偏：停止“改一处、刷一次、看一条日志”的试错方式，改为严格对照
固定版本源码和官方文档。主线 `unixnu-nx741j` 保持在上一次已推送的稳定提交；
临时 transport 探针保存在本地分支 `wip/ufs-transport-audit`，不进入主线。

## 一手来源

- [UniXNU 移植步骤](https://unixnu.com/zh/porting/steps)
- [UniXNU UFS 移植](https://unixnu.com/zh/porting/ufs)
- [UniXNU ShengUfs](https://unixnu.com/drivers/sheng-ufs)
- 固定源码 `UniXNU/ShengUfs@fd420df000f43213d9cd019725abff0849c98c72`
- [Data A Live](https://github.com/yechenan/data-a-live)，本地副本
  `_tmp/ref-data-a-live`

## 已确认的事实

### 1. 旧 `publication done=1 published=2` 的含义

旧日志使用的是：

```text
native-ufs-probe=12
native-ufs-noverify=1
nxroot-lba=246662139
adopt firmware queue
synchronous publication
```

它证明两个窗口发布成了 `IOMedia`，但不证明根卷被 APFS 选中、挂载或启动
`launchd`。因此不能把该日志当成 iOS 已启动。

### 2. 当前正常根路径为何在 `phase=1` 失败

固定版本 ShengUfs 中：

- `hostRead4k()` 首先要求 `gHost.ready`；
- `gHost.ready` 只有标准 `hostBringUp()` 完成后才置 1；
- 官方 `startRoot()` 先调用 `hostBringUp()`，再使用 `findRootLayout()`。

本机为了绕过 SM8850 固件交接问题，设备补丁改成了：

```text
board_ufs_cold_init → nxAdoptBind → nxAdoptRootLayout → startRoot
```

`nxAdoptRootLayout()` 读取 manifest 和窗口时走固件遗留队列 `nxRootRead()`，
但 `configureVerification()` 读取 `OS.vroot` / `SystemOS.vroot` 时仍调用
`hostRead4k()`。此时 `gHost.ready == 0`，所以第一条 policy table 读取返回：

```text
kIOReturnBadArgument (0xe00002c2)
NativeUfs: root preparation failed phase=1 window=0
```

已通过 9008 只读读取验证：`OS.vroot` 的 44,024,096 字节内容与本地根镜像
SHA-256 完全一致，因此不是根镜像损坏。

### 3. 不能继续把两种 transport 混用

为验证“先 cold init，再优先官方 host queue”做过一次受控实验。镜像通过 QEMU
门禁并写入手机，但下一轮 loader 没有捕获到 `NXLOG`：

```text
status: NXLGST1 found=0 len=0 matches=0
```

这说明在该 SM8850 固件交接状态下，当前标准的 `hostBringUp()` 不能直接作为
NX741J 的首选路径。继续在 adopt queue、标准 queue 和 VerifiedRoot 之间局部
打补丁，会回到 DeepSeek harness 的试错路线，不能作为主线修复。

## 两条官方及参考路线

### A. UniXNU 原生 UFS 路线

适用于有对应 OS profile、能够提供完整 VerifiedRoot 策略和稳定标准队列交接的
设备。要求按官方顺序完成：

```text
寄存器访问 → DMA/命令 → 已知块读取 → 根卷挂载 → launchd → 持久化 → 性能
```

SM8850 必须先把冷初始化、标准队列交接、SMMU 和 ownership 做成完整组合，
不能一边 adopt firmware queue、一边调用只适用于标准队列的 API。

### B. Data A Live 的 iOS 16 路线

这是与当前目标 OS 更一致的参考。它不依赖 `IOStorageFamily` 的
`IOBlockStorageDevice` 发布链，也不使用 VerifiedRoot 的 `OS.vroot` 策略表。
固定实现的做法是：

- 在 Android userdata 中创建固定的 16 GiB HFS+ 文件；
- 用 `pinfile extents --json` 取得文件块列表；
- 把列表写进 ADT `/chosen/fuxi-root-extents`；
- `sweet_ufs.c` 以 `ufsroot0` BSD 块设备暴露该文件的原始块；
- 第一次可写启动带 `ufsrw=1 rootrw=1`，让 HFS+ journal 初始化；
- `launchd` 从该 HFS+ 根卷启动。

本机已有 16 GiB 根镜像：

```text
E:\default-workspace\z80u-collect\iosroot-GOLDEN.img
```

尚不能直接使用旧 extent 表：NX741J 当前 userdata 中没有与该文件对应的固定
extent 分配。必须先恢复 Android，重新创建并固定文件，再按 data-a-live 的
`fio map` / extent 核对流程生成新表。

## 主线的确定结论

当前 NX741J 移植同时混用了：

```text
UniXNU ShengUfs archive
设备自定义 adopted firmware queue
UniXNU VerifiedRoot policy tables
data-a-live 式的 iOS 16 目标
```

这四部分不属于同一条已验证路线，不能靠继续加单个探针解决。

下一步应采用 **Data A Live 的 iOS 16 固定 extent + HFS+ `ufsroot0` 路线**：

1. 恢复 Android，确认 userdata f2fs、root 和 `pinfile` 可用；
2. 创建 16 GiB 固定文件并生成 NX741J 自己的 extent 表；
3. 将 data-a-live 的 UFS 驱动和 HFS 根设备补丁按固定 patch series 移植；
4. 先用只读 extent 测试，再写 `iosroot-GOLDEN.img`；
5. 通过 `ufsroot0` 挂载 HFS+，验证 `launchd`；
6. 全部通过后再恢复第 7 步的 USB/Wi-Fi/音频/基带等外设。

在此之前不再向手机写入新的实验 payload。
