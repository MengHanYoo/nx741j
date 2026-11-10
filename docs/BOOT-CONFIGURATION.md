# 启动配置（nx741j）

对应官方[启动配置](https://unixnu.com/zh/reference/boot-configuration)：payload 卷中的
`\EFI\UniXNU\unixnu.toml` 由 `board.toml` 的 `[payload]` / `[payload.ui]` 生成，修改后
重新执行 `./unixnu payload` 与 `./unixnu bootimg`。

## board.toml → unixnu.toml

| board.toml | 生成值 | 官方语义 |
|---|---|---|
| `[payload] timeout = 5` | `[boot] timeout = 5` | 倒计时秒数 |
| `[payload] rotate = 0` | `[boot] rotate = 0` | 空闲轮换关闭 |
| `[payload] fallback_timeout = 10` | `[boot] fallback_timeout = 10` | **无输入设备**时的超时（本机当前无 UEFI 输入） |
| `[payload] connect_all = true` | `[boot] connect_all = true` | 对所有 handle 调 `ConnectController` |
| — | `[boot] default = "xnu"` | 由生成器写入，指向 xnu 菜单项 |
| `[payload] title` | `[ui] title = "NX741J"` | 菜单标题 |
| `[payload.ui] scale = 0` | `[ui] scale = 0` | 字形缩放自动 |
| `[payload.ui] logo = true` | `[ui] logo = true` | 绘制标志 |
| `[payload.ui] layout = "auto"` | `[ui] layout = "auto"` | 1216×2688 走竖屏单列布局 |
| `[payload.ui] portrait_below = 800` | `[ui] portrait_below = 800` | 宽/高比阈值 |
| `[payload.ui] console = "auto"` | `[ui] console = "auto"` | 控制台模式 |
| `[payload] kernel_args` | `[[entry]] args` | `-v serial=3 keepsyms=1 debug=0x144` |
| `[payload] panic_screen = true` | 内核补丁 | panic 屏幕 |
| `[payload] loader = "firmware"` | 无 `[[driver]]` | DarwinBoot 在固件卷 |
| `[[payload.block_slice]]` | `[[block_slice]]`（partition/name/offset/length/writable/enabled） | 把 userdata 的窗口发布成整盘块设备，供分区/文件系统驱动绑定（阶段 6） |

菜单项：`xnu`（`type = "xnu"`、`Payloads/kernel.macho`、`initrd = "Payloads/scratch-data.img"`），
以及生成器追加的 `Restart` / `Power off` 两个 action。

## 生成器覆盖面（官方 11 项特性）

官方 `reference/boot-configuration` 列了 11 项特性（`[boot]`、`[ui]`、`[[driver]]`、`[[dt_patch]]`、
`[[block_slice]]`、`[acpi]`、kit（`[[kit]]`+`[kits]`）、`[[entry]]`、`[entry.chosen]`、`[[entry.kext]]`、`[[entry.variable]]`）。
`scripts/unixnu/payload.py` 的生成器覆盖 6 行（下表 ✅），另 6 行（❌）没有 `board.toml` 键；其中官方的 kit 一节拆成 `[kits]`（支持）与 `[[kit]]`（不支持）：

| 官方特性 | 生成器 | 本机 | 说明 |
|---|---|---|---|
| `[boot]` | ✅ | ✅ 5 键 | timeout/rotate/fallback_timeout/connect_all/default |
| `[ui]` | ✅ | ✅ 6 键 | 1216×2688 走竖屏单列布局 |
| `[kits]` | ✅ | ✅ `autoload = false` | 设备侧不自动加载 kit（QEMU 的 ESP 才 true） |
| `[[driver]]` | ✅ 仅当 `loader != "firmware"` | ❌ | DarwinBoot 由固件卷的 loader protocol 提供 |
| `[[block_slice]]` | ✅ 2026-11-11 起（`[[payload.block_slice]]`） | 待阶段 6 | 根卷窗口发布 |
| `[[entry]]` | ✅ | ✅ xnu + reset/shutdown/tools | 未用 confirm/dtb/initrd/enabled |
| `[[dt_patch]]` | ❌ | ❌ | ADT 由 `devicetree.py` 从实机 DTB 生成；Darwin 走 `.adt` sidecar |
| `[acpi]` | ❌ | ❌ | arm64 XNU 不消费 ACPI 表（AcpiTableDxe 只服务于 EFI/Linux 路径） |
| `[[kit]]` | ❌ | ❌ | 显式 kit 列表未实现；`[kits]` 的 autoload 可用 |
| `[entry.chosen]` | ❌ | ❌ | 命令行走 `args`，不需要 /chosen 注入 |
| `[[entry.kext]]` | ❌ | ❌ | kext 预链接进内核 |
| `[[entry.variable]]` | ❌ | ❌ | 不使用 UEFI 变量注入 |

**阶段 6 的前置**：根卷需要把 userdata 的 SHNGROOT/OS/SystemOS 窗口以整盘块设备发布，
这只能用 `[[block_slice]]`。该映射已于 2026-11-11 补进 `payload.py` 的 `render_block_slices()`：

```toml
[[payload.block_slice]]
partition = "userdata"
name = "SHNGROOT"
offset = 0            # 相对分区起始的字节偏移
length = 0x14000000   # 0 = 到分区末尾
writable = false
enabled = true
```

校验：`partition`/`name` 必填；`offset`/`length` 为非负整数；`writable`/`enabled` 为布尔；
未配置时生成器输出空串，现有镜像逐字节不变。

## 菜单行为（官方）

| 条件 | 结果 |
|---|---|
| 有输入设备 | 倒计时到 `timeout` |
| 无输入设备 | 使用 `fallback_timeout`（当前 10 秒） |
| 启动失败 | 返回菜单，不再倒计时 |

## 验证

```sh
./unixnu payload -D ../UniXNU-nx741j
mtype -i out/nx741j/ios-16.7.16-20H392/nx741j/payload/payload.img ::/EFI/UniXNU/unixnu.toml
```

实际生成内容（`payload.img` 内）：

```toml
[boot]
timeout = 5
rotate = 0
fallback_timeout = 10
connect_all = true
default = "xnu"
[ui]
title = "NX741J"
scale = 0
logo = true
layout = "auto"
portrait_below = 800
console = "auto"
```

设备仓库 revision `4635358365cd2c30523de0207be509b7fd30b059`。

## 存疑与后续

- 本机尚无 UEFI 输入设备，菜单依赖 `fallback_timeout`；按键就绪后可改为 `timeout = -1`（有输入时等待按键）。
- ~~`[[block_slice]]` 未声明~~：映射已补（`[[payload.block_slice]]`），阶段 6 填窗口即生效；`[[driver]]`、`[acpi]`、`[[kit]]` 仍不适用（见上表）。
