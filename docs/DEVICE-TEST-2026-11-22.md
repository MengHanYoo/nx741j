# 真机测试记录 — 2026-11-22

## 结论

根镜像、尾部锚点、payload、UEFI FD 和旧 `efisp-p7` loader 都已通过 9008
写入并回读；复位后没有进入 Android 或 iOS。Windows 看到：

```text
QUSB_BULK_CID:046A_SN:098F0FC3
USB\VID_19D2&PID_0112
```

该设备没有绑定 9008 串口，ADB 也没有设备。重新进入 9008 后读取到旧
loader 捕获的 XNU 日志，证明内核已经完成 early VM/kext collection 并进入
NativeUfs；但仍不能声称 APFS 或 `launchd` 已在真机启动。

## 已完成并回读的写入

| 区域 | 范围 | 内容 | 结果 |
|---|---|---|---|
| userdata 根镜像 | LUN0 sector `246662139..248724071` | `userdata-root.img` 8,445,677,568 B | 全量回读 SHA-256 `f45c3288b555a43f945c78a8c75ad56343039f3f5f5350fe57316c64539d2b70` |
| userdata 锚点 | LUN0 sector `248838138` | `out/userdata-anchor.bin` 4 KiB | 回读一致；manifest `246662139`，manifest SHA `443e0fda…` |
| rawdump payload（第一次） | LUN0 sector `4743976` | 旧 normal-root payload | 回读 SHA-256 `219817f9…` |
| rawdump FD | LUN0 sector `4760360` | `out/final-pmgr/SILICIUM_UEFI.fd` | 回读 SHA-256 `3ce5c6d2…` |
| efisp loader（旧） | LUN4 sector `748894` | 旧 `loader/images/efisp-p7.img` | 历史回读 SHA-256 `e61814d4…` |
| rawdump probe=12（旧） | LUN0 sector `4743976` | 当时的 `payload-probe12.img` | 13:13 写入并完整回读 SHA-256 `8221c727…` |
| efisp loader（当前实机） | LUN4 sector `748894` | 修复后的 `loader/images/efisp-p7.img` | 13:32 写入并完整回读 SHA-256 `73b56bf2…` |
| rawdump probe=12（当前实机） | LUN0 sector `4743976` | 最终 `payload-probe12.img` | 13:32 写入并完整回读 SHA-256 `fa777aeb…` |

## 旧 loader 捕获的内核日志

2026-11-22 重新进入 9008 后读取：

| 区域 | LBA | 结果 |
|---|---|---|
| loader progress | `4764200` | 全零，暴露 `gRawBase` 未初始化 |
| NXLOG status | `4764464` | `NXLGST1`，捕获 14,603 B |
| NXLOG 正文 | `4764465..4764468` | 4 KiB × 4，合并后为完整捕获内容 |
| UFS snapshot | `4764488` | 全零，因为旧 loader 没有调用 `dump_ufs_state()` |

日志中的有效顺序包括：

```text
vm_page_bootstrap: 111123 free pages, 18981 wired pages
OSLog stream configured: stream: 8192 bytes
No POST boot-arg set.
NATIVE_UFS start provider=nubia,nx741j
NATIVE_UFS bringup: enter
NATIVE_UFS bringup: hc=fffffff6bc035000 cap=d5c7031f hcs=0000010f hce=00000001
```

这说明 QUSB_BULK 不是“loader 没跑”或“只到 bring up enter”的同义词：
XNU 已经运行到存储驱动，随后旧正常根路径错误进入 `hostBringUp()`。

## 复位后的观测

- Android ADB：无设备。
- `Get-PnpDevice`：出现 `QUSB_BULK_CID:046A_SN:098F0FC3`，
  `Present=True`，状态为 Error，没有 COM 端口。
- 该 USB 硬件 ID 是 `VID_19D2&PID_0112`；它不是正常 Android
  `VID_19D2&PID_0306`，也不是 9008 的 `VID_05C6&PID_9008`。
- 旧日志显示 payload/FD 读取、跳转、early VM 和 kext collection 都已执行；
  最后的明确应用日志是 NativeUfs，而不是 loader 阶段。

旧、新两次 `probe=12` 对照 payload 启动后，设备都再次枚举为同一
QUSB_BULK；要读取最终这轮日志必须先让设备重新进入 9008。当前新产物为：

```text
kernel.macho           255cdf323d1d00bc5e077a628d9f6687c6f354de7dc2d9739f2923a70dbd3c9b
normal-root payload    8801fbd728c35227c193d2aac8dfcf1302ea02a5b2f47c55b07c19f9ab0de95c
probe=12 payload       fa777aeb86eab4b60c573b3086d2b309ffeed1972f00b4a91e18c34337cf0036
boot.img               dbb6c35c9e133c35fe3b7c2d52a4b129239db68ca3d3018309acce7aa10e004d
efisp-p7.img (fixed)   73b56bf2b2192706ec724974f832457d5b95ab1382038e3442bc392127b4b246
```

## 已修复的 loader 误写

旧 `loader/p7.c` 的 `write_progress()` 使用 `gRawBase`，但主流程从未给它
赋值。于是“rawdump + 79 MiB”实际变成绝对 LBA `20224`。本机 GPT 中
LBA `20224` 位于 `super`（`8968..4727559`）。

源码已修复为：

- `find_partition("rawdump")` 成功后显式设置 `gRawBase` 和 `gRawBs`；
- 任一基址/块大小为零时拒绝写 progress；
- 启用 `dump_ufs_state()`，让 LBA `4764488` 真正保存 UFS snapshot。

2026-11-09 的 `super.img` 备份中，对应扇区 SHA-256 为
`068f6bf100cff09305a6eaad2ef72522af3647f16b9c5a2c24c0251cbff0c2ee`。
`scripts/host/repair-super-progress-sector.ps1` 已只读当前 LBA `20224`；
结果与备份完全一致，因此没有执行写回。脚本仍保留 fail-closed 的精确恢复能力，
且不会重写整个 `super`。

## 优先恢复与下一步

先在设备侧强制断电并重新进入 EDL（电源键 + 音量组合；具体按设备当前
按键状态执行），不要继续向未知分区写入。进入 9008 后：

1. 重新进入 9008 后读取新 loader 的 progress、NXLOG 和 UFS snapshot。
   `super` LBA `20224` 已核对为备份原值；只有在以后再次异常时才运行：

   ```powershell
   powershell -File E:\default-workspace\z80u-collect\wsl\repair-super-progress-sector.ps1 `
     -SkipSahara -NoReboot
   ```

2. 读取 rawdump 的 loader 进度记录：

   ```powershell
   powershell -File E:\default-workspace\z80u-collect\wsl\read-9008.ps1 `
     -StartSector 4764200 -Sectors 1 `
     -OutFile E:\default-workspace\z80u-collect\_tmp\device-read-20261122\loader-progress.bin `
     -Label rawdump -Lun 0
   ```

3. 读取新 p7 的 log/status 记录（LBA `4764464` 起）以及 UFS
   snapshot（`4764488`）。
4. 如只需恢复 Android，刷回原厂 `efisp.img`（SHA-256
   `4df183594a63af28c681e48b1d9fdb0305ee055ea8fde92118a1e0af7a5f6f17`），
   不要重写 `userdata`。
5. 读取最终 probe 日志后，再决定是否切换到 normal-root payload。

## 边界

- `out/final-pmgr/` 是主机产物和回读依据，不表示设备已完成启动。
- PMGR 报告仍是 `runtime_qualified=false`。
- 第 7 步和持久 Data/性能仍冻结。
