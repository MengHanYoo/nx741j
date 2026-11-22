# 真机测试记录 — 2026-11-22

## 结论

根镜像、尾部锚点、payload、UEFI FD 和 `efisp-p7` loader 都已通过 9008 写入并回读；
但复位后没有进入 Android 或 iOS。Windows 看到：

```text
QUSB_BULK_CID:046A_SN:098F0FC3
USB\VID_19D2&PID_0112
```

该设备没有绑定 9008 串口，ADB 也没有设备。因此本轮不能声称 iOS、APFS 或
`launchd` 已在真机启动。

## 已完成并回读的写入

| 区域 | 范围 | 内容 | 结果 |
|---|---|---|---|
| userdata 根镜像 | LUN0 sector `246662139..248724071` | `userdata-root.img` 8,445,677,568 B | 全量回读 SHA-256 `f45c3288b555a43f945c78a8c75ad56343039f3f5f5350fe57316c64539d2b70` |
| userdata 锚点 | LUN0 sector `248838138` | `out/userdata-anchor.bin` 4 KiB | 回读一致；manifest `246662139`，manifest SHA `443e0fda…` |
| rawdump payload | LUN0 sector `4743976` | `out/final-pmgr/payload.img` | 回读 SHA-256 `219817f9…` |
| rawdump FD | LUN0 sector `4760360` | `out/final-pmgr/SILICIUM_UEFI.fd` | 回读 SHA-256 `3ce5c6d2…` |
| efisp loader | LUN4 sector `748894` | `loader/images/efisp-p7.img` | 回读 SHA-256 `e61814d4…` |

## 复位后的观测

- Android ADB：无设备。
- `Get-PnpDevice`：出现 `QUSB_BULK_CID:046A_SN:098F0FC3`，
  `Present=True`，状态为 Error，没有 COM 端口。
- 该 USB 硬件 ID 是 `VID_19D2&PID_0112`；它不是正常 Android
  `VID_19D2&PID_0306`，也不是 9008 的 `VID_05C6&PID_9008`。
- 因此本轮最后完成阶段是 loader/固件尝试的 USB 枚举，尚不能定位是
  payload/FD 读取、FD 拷贝、跳转还是 XNU 早期失败。

## 优先恢复与下一步

先在设备侧强制断电并重新进入 EDL（电源键 + 音量组合；具体按设备当前
按键状态执行），不要继续向未知分区写入。进入 9008 后：

1. 用 `wsl\read-9008.ps1` 读取 rawdump 的 loader 进度记录：

   ```powershell
   powershell -File E:\default-workspace\z80u-collect\wsl\read-9008.ps1 `
     -StartSector 4764200 -Sectors 1 `
     -OutFile E:\default-workspace\z80u-collect\_tmp\device-read-20261122\loader-progress.bin `
     -Label rawdump -Lun 0
   ```

2. 读取 p7 的 log/status 记录（LBA `4764464`、`4764465`）以及 UFS
   snapshot（`4764488`）。
3. 如只需恢复 Android，刷回原厂 `efisp.img`（SHA-256
   `4df183594a63af28c681e48b1d9fdb0305ee055ea8fde92118a1e0af7a5f6f17`），
   不要重写 `userdata`。
4. 只有读到最后完成阶段后，才重新调整 loader/FD/payload 或切回 probe
   payload。

## 边界

- `out/final-pmgr/` 是主机产物和回读依据，不表示设备已完成启动。
- PMGR 报告仍是 `runtime_qualified=false`。
- 第 7 步和持久 Data/性能仍冻结。
