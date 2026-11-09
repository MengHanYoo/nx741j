# 层 4 探针（V67 → V86）

层 3→4 曾经完全黑屏（菜单倒计时后直接 `ZTE MemoryDump Mode`）。V67/V75 用“屏幕打印 + 停顿”把链路推到
`DARWINBOOT: Boot called`；V76/V77 推到了 `kernel … bytes` / `adt … window …`，之后又是黑屏。
本页记录当前这一版（V86）的探针。

## V86：把 `adt … window …` 之后的每一步都打出来

最后一次实机可见打印是 `DARWINBOOT: adt … window …`，下一个 `Print` 要到 `ARM64 va=…`。中间这段
（读 video、分配窗口、清内存、拷内核、解 ramdisk、写进度、关根、取内存映射）**没有任何输出**，
一旦挂死或复位就只剩黑屏。V86 在每一步之间加一行 `Print`。

| 打印 | 位置 | 缺失说明什么 |
|---|---|---|
| `p1 assets …` | 读完 trustcache/gpuaddon/vroot 之后 | 读 sidecar 卡住（`.trustcache`/`.gpuaddon`/`.vroot`） |
| `p2 video base …` | `ReadBootVideo` 之后 | GOP/video 查询失败或卡住 |
| `p3 window at … size …` | `AllocateWindow` 之后 | 窗口分配失败（内存不足/对齐） |
| `p4 window layout used …` | 布局计算之后、越界检查之前 | 布局计算异常 |
| `p5 zeroing …` | `ZeroMem` 之前 | `ZeroMem` 自身卡住（下一行不出现） |
| `p6 copying N segments` | 拷贝内核段之前 | 段表异常（N 不对说明 Mach-O 解析有问题） |
| `p7 kernel image in place` | 段拷贝之后 | 拷 51 MB 过程中出错/复位 |
| `p8 ramdisk …` / `p9 ramdisk in place` | 解压 payload ramdisk 前后 | 解压（64 MB LZSS）卡住 |
| `p10 assets copied, writing step 7` | `DbProbeWrite` 之前 | 进度写盘是否卡住 |
| `p11 closing the root` | 关闭文件系统根之前 | FAT 关闭异常 |
| `p12 map N bytes key …` | `GetMap` 之后 | 内存映射获取失败 |
| `p13 attempt N map -> …` | 每次 `GetMemoryMap` 之后 | 映射在 ExitBootServices 前变化 |
| `p14 attempt N exit boot services -> …` | 每次 `ExitBootServices` 之后 | —— |
| `p15 entering the kernel at …` | 跳转前 | p15 出现后黑屏说明已进内核（层 5），改看 green bar/panic |

两个已知的“静默死点”这次有了区分手段：`ExitBootServices` 三次 `EFI_INVALID_PARAMETER` 会进 `CpuDeadLoop()`
（现在能看到 `p13`/`p14`/`p14b`）；`DarwinEnter` 之后没有固件输出（`p15` 是最后一行，黑屏属预期）。

## 构建与刷写（需要 root）

`/opt/work/uefi-sheng-main` 归 root，普通会话建不了 FD。root 会话：

```sh
cd /home/loongko/nx741j-build/UniXNU
sudo ./unixnu firmware -D /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
./unixnu payload  -D /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j -j 16
./unixnu bootimg  -D /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
```

`package/upstream/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c` 会被 `package/firmware-preflight.py`
覆盖进构建树，改这个文件就够了；`payload`/`bootimg` 会带上新 FD（`boot.json` 的 `fd_sha256` 会变）。
刷写走 `z80u-collect/wsl/flash-boota-9008.ps1`（FD LUN0 LBA 4760360）。

## 判读

1. 菜单选 XNU，看最后一行 `pN`；
2. 缺失的那一行就是断点，按上表定位；
3. 到 `p15` 才黑屏 → 层 4 通过，转层 5（green bar / panic 文本 / `serial=3` 日志）。

## 历史

| 版本 | 打印/停顿 | 结果 |
|---|---|---|
| V67 | `UNIXNU:` / `DARWINBOOT:` 关键步骤 + 10–20 s 停顿 | 停顿触发 BDS 看门狗，MemoryDump；固件 `764c388a…` |
| V75 | 去掉长停顿、kext 为空时跳过 | 出现 `DARWINBOOT: Boot called`，层 4 打通到 DarwinBoot 内部 |
| V76 | 不再传 DeviceTree，改 payload 的 `.adt` sidecar；10 s → 1 s | 实机见 `kernel … bytes`、`adt … window …` |
| V77 | video/window/copy/map/ExitBootServices 之间加打印 | FD `d49993ff…`，待验证 |
| V86 | 本次：`p1`–`p15` 逐段打印 | FD `dea20e0d…`，boot.img `5828e5a2…`，待实机 |

## V86 已编进固件（2026-11-09 验证）

`sudo ./unixnu firmware`（root，22 s 增量）→ `DarwinBoot.efi` 重建（sha256 `f5d5c883…`），
`FVMAIN.Fv` 里能查到 `p1 assets` 与 `p15 entering the kernel`（`DARWINBOOT` 共 29 处）。

注意：最终 `SILICIUM_UEFI.fd` 是 **LZMA 压缩的 FVMAIN_COMPACT**，直接对 fd 做 `strings` 找不到任何模块字符串——
要验证得看 `Build/nx741jPkg/DEBUG_CLANGPDB/FV/FVMAIN.Fv` 或模块自己的 `.efi`：

```sh
strings -el Build/nx741jPkg/DEBUG_CLANGPDB/FV/FVMAIN.Fv | grep -c 'p1 assets'
```

| 产物 | sha256 |
|---|---|
| FD | `dea20e0d8d343e1b46aa160911e0fda94fc5f9b650bd8dcf7ceacb6a633e73e2` |
| DarwinBoot.efi | `f5d5c883531416b5ae288ab582ce7cfa11a49918511858a95accf65482365961` |
| boot.img | `5828e5a2a053a521b2fc0e3d314f73095a12af0552cf5a0f72013676c43b732c` |
| payload.img | `a68ebc4e9b00bf43d4b6f19c1b197e46df6c4743b6b7ae64dee1fd1ec7a22e10` |

源码均在 `package/upstream/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c`，随设备仓库版本控制。
