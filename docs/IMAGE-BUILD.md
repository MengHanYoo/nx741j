# 镜像构建：从原始镜像 + 补丁集构建（2026-11-02）

## 为什么改成这样

之前所有修复都是直接在成品 `iosroot-SEAL.img` 上用 QEMU 原地改：换插件、删 AirPlay HAL、装 vktest、转 PNG 壁纸、改 assetsd plist……**没有任何清单**。后果已经出现过：

- 改的是 PC 上的 SEAL、手机没推 → 两轮插件测试白跑；
- 17 GB 推送被中断 → 手机里的镜像变成新旧混合的半写状态；
- 无法判断某个现象来自哪次改动，也无法回滚到某个已知状态。

正确做法：**只读 BASE + 有序补丁集 → 构建 → 校验 → 推送**。

## BASE 是什么

由 `mkiosroot.sh` 从 IPSW 的三个 DMG 全量构建的 HFS+ 根镜像（`iosroot-GOLDEN.img` 即当前基底，10/28 生成）。BASE 永不修改。

## 补丁集（BASE=GOLDEN vs 运行时 SEAL 的完整差异）

用 `7z l -slt` 导出两个镜像的完整清单（各约 33 万条目）后逐项比对，差异如下。

### 新增（22 个文件 + 9 个目录）

| 组 | 内容 |
|---|---|
| GPU 插件 | `NativeAdrenoMetal.bundle/NativeAdrenoMetal`（22,126,160 B，plugin7）、`_CodeSignature/CodeResources`、`libvulkan_freedreno.dylib`（17 MB） |
| 插件开关 | `NativeAdrenoMetal.clients`、`NativeAdrenoMetal.publish` |
| Turnip / 测试 | `usr/lib/libvulkan{,.1,.so.1,_freedreno}.dylib`（各 17 MB）、`usr/bin/vktest`、`usr/libexec/vktest`、`com.nx741j.vktest.plist` |
| 壁纸 | 5 张 PNG（Head Mask/Head/Leg/Arm/Body） |
| AirPlay HAL 备份 | `private/var/hal-backup/{AirPlayHalogen,AppleTimeSyncAudioClock}.driver`（含 `_CodeSignature`） |

### 删除（11 项）

- `System/Library/Audio/Plug-Ins/HAL/AirPlayHalogen.driver`（整个目录）
- `System/Library/Audio/Plug-Ins/HAL/AppleTimeSyncAudioClock.driver`（整个目录）
- `private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist`

### 修改（6 个文件）

| 文件 | 变化 |
|---|---|
| `NativeAdrenoMetal.bundle/NativeAdrenoMetal` | 22,438,240 → 22,126,160 B |
| 3 个壁纸 `main.caml` | HEIC 引用 → PNG |
| `com.apple.assetsd.plist` | 1174 → 1202 B（调试用：stderr 指向 /dev/console） |
| `private/var/db/com.apple.xpc.launchd/disabled.plist` | 723 → 403 B |

## 目录与用法

```
repos/nx741j/image/
├── manifest.json          # 安装(28)/建目录(9)/删除(11)，含 sha256、mode、owner
├── make_initramfs.py      # 生成应用脚本 + 打包 initramfs（WSL/root）
├── apply_image.sh         # QEMU 把 initramfs 应用到目标镜像
└── build_image.ps1        # 一键：BASE→OUT→应用→校验
```

```powershell
# 默认 GOLDEN -> iosroot-BUILD.img
pwsh -File repos\nx741j\image\build_image.ps1

# 指定输入输出
pwsh -File repos\nx741j\image\build_image.ps1 -Base <BASE.img> -Out <OUT.img>
```

大文件（插件、Turnip、PNG、vktest）不进 git：放在 `z80u-collect\image-artifacts\`，由 `manifest.json` 里的 sha256 固定内容。

## 后续规则

1. 任何修复都进补丁集（manifest + artifacts + 小文件），**不碰 BASE、不在成品上再改**；
2. 构建后用 7z 校验三个 marker（插件大小、PNG 存在、plist 大小）；
3. 校验通过才 `write_image.ps1` 推送；推送中断必须整份重推（extents 顺序写，中断=新旧混合）。

## 验证结果（2026-11-02 14:05）

第一次构建时 `Head Mask.png`（文件名带空格）被安装脚本漏掉——`cp` 没加引号被拆成两个参数。加引号重建后：

```
seal entries: 331635   build entries: 331635
only in SEAL: 0   only in BUILD: 0   size-changed: 0
```

即 `GOLDEN + manifest` 与手工累积出来的 `iosroot-SEAL.img` **逐文件一致**（331,635 个条目）。从今以后：

- 运行时镜像 = `BUILD`（由 BASE + 补丁集生成），不再手工改 SEAL；
- 任何修复都以补丁形式进 `image/manifest.json` + `image-artifacts/`；
- 重建后先比对 marker，再 `write_image.ps1` 推送。
## 推送提速（2026-11-03）

`write_image.ps1` 从「`adb exec-in` 管道 + `pinfile rawwrite`」改成「`adb push` 暂存 + 设备端 `pinfile rawimport`」：

| 阶段 | 旧 | 新 |
|---|---|---|
| 传输 | stdin 管道 56 MB/s | `adb push` **321 MB/s**（USB super-speed-plus） |
| 写入 | 串行 + 每块 SHA256，55 MB/s | `rawimport` 多线程 **1,450 MB/s** |
| 17 GB 总计 | 370 s | **70 s** |

`pinfile` 新增 `rawimport PATH DEV STAGE --yes`：从设备上的暂存文件读、并行写进钉住文件的 extents（`PINFILE_THREADS` 默认 8，`PINFILE_BUFFERED=1` 可关 O_DIRECT）。源码在 `repos/data-a-live/tools/pinfile/pinfile.c`，用 NDK r27c 的 `aarch64-linux-android31-clang.cmd -static` 编译。


