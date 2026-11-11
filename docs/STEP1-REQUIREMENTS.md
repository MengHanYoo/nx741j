# 步骤 1「准备主机和仓库」对照：官方文档 × pandora × NX741J

依据：`guide/requirements`、`reference/configuration#local-build-inputs`、`building`、
`porting/log-capture`、`concepts/security`；参考实现 `repos/data-a-live`（小米 17 Pro，SM8850，已进 iOS）。

## 1. 主机（requirements#主机）

| 项 | 官方 | 我们（无 macOS） | pandora |
|---|---|---|---|
| 内核构建 | macOS + Xcode；Linux 用 `scripts/linux/setup-toolchain.py` 装 clang+cctools 到 `/opt/unixnu-linux` | ✅ 走 Linux/WSL，工具链已装（clang 24 + 38 个 Mach-O 工具） | 内核在 GitHub **macOS runner**（`xnu.yml`）编，工具在 WSL 跑 |
| `--skip-setup` / `seed-data-grow` | 仅 macOS（`hdiutil`/`diskutil`） | ❌ 不可用 | 不涉及 |
| 无 Mac 的替代 | building#macos-runner：GitHub macOS runner 只出脱敏日志 | 未启用 | ✅ 正是这条路 |

结论：主机满足；pandora 的 CI 方案可作为内核构建可复现性的备选。

## 2. 外部输入（requirements#外部输入）

| 输入 | 官方选项 | 我们 | 状态 |
|---|---|---|---|
| Mu-Silicium 仓库 | `--uefi` / `UNIXNU_UEFI` | `/opt/work/uefi-sheng-main` | ✅ |
| 目标 IPSW | `--ipsw` / `UNIXNU_IPSW` | `[ipsw] url` + 本地解压；**local.toml 未配 ipsw** | ⚠️ 用解压目录替代 |
| kernelcache | `--kernelcache` | `/opt/work/ipsw/kernelcache.arm64` | ✅ |
| 设备树 | `--dtb` | FdtBlob 实机 DTB | ✅ |
| trust cache | `--trust-cache` | `build/trustcache.bundle`（**自制扁平包**） | ⚠️ 非 IPSW 原包 |
| owner trust cache | `--owner-trust-cache` | 未用 | ⬜ |
| Data seed | `--data-ramdisk` | 未用 | ⬜（阶段 6） |
| 平台种子 | `--unique-chip-id` | `00000000098EE0C3` **占位值** | ❌ 必须是真机值 |
| 预链接内核 | `--kernel` | prelink 产物 | ✅ |
| edk2 / Mu_Basecore | `--edk2` | `/opt/work/uefi-sheng-main/Mu_Basecore` | ✅ |

## 3. 目标 IPSW（requirements#设备）

官方把 iPhone X **16.7.16 (20H392)** 列为第二目标（sheng 默认 iPadOS 17.7.11）；我们只做 16.7.16，
`[ipsw.sha256]` 固定 4 个成员，`kernelcache.release.iphone10b` 已 pin。pandora 同样是 iPhone X + iOS 16.7.x。

## 4. 设备前提——**最大的偏差**

官方要求：bootloader 可解锁并**接受 `fastboot boot`**；UEFI 提供 framebuffer/输入/块 I/O/文件系统；
bring-up 有 **UART 或 USB 日志通道**；内存足够；IPSW 与设备身份匹配。

| 要求 | 我们 |
|---|---|
| 接受 `fastboot boot` | ❌ **ABL 拒绝第三方 boot.img**，`fastboot boot` 回退；改走 efisp → FD → UniXNU（`docs/BOOT-FIRST.md`） |
| UEFI 四件套 | ✅（FV 内已核对：UniXNU/UniXNUDxe/DarwinBoot/UniXNURamDiskDxe/DiskIoDxe/Fat） |
| UART 或 USB 日志 | ❌ 无 UART，靠屏幕（panic 屏/彩带）；USB 串口未接 |
| 内存 | ✅ 16 GB，交接窗口 256 MiB |
| 身份匹配 | ⚠️ IPSW 对（iPhone10,3），但 `unique_chip_id` 是占位 |

pandora 的设备**满足**这条前提（`fastboot boot` 可用），所以它的移植流程能直接照文档走；
我们不满足，这是整条链必须用 efisp 的根本原因。

## 5. 本地构建配置（configuration#local-build-inputs）

`unixnu.local.toml` 已建：`device`/`uefi`/`edk2`/`dtb`/`fd`/`kernelcache`/`trust_cache`/`jobs`/`target`/`kernel_args`/`unique_chip_id`。
缺 `ipsw`、`data_ramdisk`、`owner_trust_cache`；`ramdisk = "none"` 是有意（ABL 不接受 boot 镜像内 ramdisk）。
优先级（命令行 > 环境变量 > 本地文件 > 设备默认）与官方一致。

## 6. 构建五步（building）

| 官方步骤 | 我们 | pandora |
|---|---|---|
| 1 clone + defconfig + fetch + status | ✅ `nx741j_defconfig`，`status --json` rc=0 | fork + CI |
| 2 local.toml + `ipsw info/fetch` + `tree` | ⚠️ 无 `ipsw fetch`（用本地解压） | `tools/` 脚本解 IPSW |
| 3 build kernel/package/firmware | ✅ kernel 158 s、payload、firmware | kernel 在 CI；不用 UniXNU FV |
| 4 userdata + payload | payload ✅，**userdata 未做** | 根卷=Android `/data` 文件 |
| 5 bootimg + 单次启动 | ✅ 但**不走 fastboot**，走 9008 rawdump | `fastboot boot` ✅ |

## 7. 抓取日志与自动启动测试（log-capture 六步）

| 步骤 | 我们 |
|---|---|
| 1 集成 `AdvancedLogDxe` | ✅ 在 FV（DSC:111 / DXE.inc:89） |
| 2 Tools→Advanced Logger + QR 扫描 | ❌ **阻塞**：Mu 的 `LocateHandleBuffer(AllHandles)` 返回 0，导出器拿不到日志缓冲 |
| 3 记录内核交接 | ⚠️ 屏幕（panic 屏/彩带）+ 入口探针复位，无 UART |
| 4 持久化日志 | ⬜ 未做（阶段 6） |
| 5 手动循环 | ⚠️ 9008 刷机 + 拍照 |
| 6 自动循环 | ⬜ 未做 |

pandora 没有 QR 路径，靠屏幕照片 + 内核 `printf`（`sweetfb`）——可作为我们的临时替代。

## 8. 安全输入（concepts/security）

| 输入/机制 | 官方 | 我们 | 风险 |
|---|---|---|---|
| trust cache 包 + sha256 pin | 原包 + `[payload] trust_cache_sha256` | **自制扁平包** + sha pin | ⚠️ 偏离 |
| 平台种子 | 真机 `--unique-chip-id` | **占位值** | ❌ 高 |
| ADT 身份 | 类型化 `[adt]` | 实机 DTB 派生 | ✅ |
| 4 KiB 签名 | `native-4k-signature-hash-cache` | 未启用 | ⬜ 待确认 |
| AMFI/Image4 | `amfi-only-platform-code` + early_security_init | **0003 跳过 loadSecurityExtensions、0004 trustcache without Image4、0011 Image4 视为不存在** | ❌ 安全路径被绕过（bring-up 临时策略） |
| verified root | VerifiedRoot kit + 策略包 | kit 已链接、策略本地生成、fail-closed | ✅ 部分 |

## 9. 与 pandora 的路线差异

| 环节 | pandora | 我们 | 原因 |
|---|---|---|---|
| 启动 | `fastboot boot`（不写分区） | efisp → rawdump payload/FD | ABL 拒绝第三方 boot.img |
| 根卷 | Android `/data` 里的固定文件 + extent 表 | 官方 SHNGROOT + `root_match_volume_uuid` | 本机不能读 F2FS 文件、也不走 fastboot |
| 安全 | 同样打补丁跳过 Image4/AMFI | 同 | 同类 bring-up 策略 |
| 内核构建 | GitHub macOS runner | 本地 WSL clang+cctools | 无 Mac |

可借鉴：`mkprelink.py`/`boot_ios.sh` 的输入组织、CI macOS runner、`sweet_ufs` 接管序列（已单独分析）。

## 10. 结论：按官方要求还差什么

1. **`fastboot boot` 前提不满足** → 已在 `BOOT-FIRST.md` 记录偏差，但应同时在 `porting.toml` 的 limits 里显式声明；
2. **`unique_chip_id` 是占位值** → 设备在 Android 时一条命令可取：`adb shell getprop ro.boot.unique_chip_id`，取到后替换 `unixnu.local.toml`；
3. **trust cache 自制包 + 绕过 Image4/AMFI** → 属 bring-up 临时策略，需在安全记录里标明，正式启用前恢复官方路径；
4. **日志通道**：无 UART + QR 导出被 Mu 限制阻塞 → 短期用屏幕拍照（pandora 同法），长期解决 `LocateHandleBuffer` 限制；
5. **userdata/SHNGROOT 未做**（阶段 6）；
6. `unixnu.local.toml` 补 `ipsw`（或说明只维护解压目录）。
