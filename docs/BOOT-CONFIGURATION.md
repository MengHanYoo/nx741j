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

菜单项：`xnu`（`type = "xnu"`、`Payloads/kernel.macho`、`initrd = "Payloads/scratch-data.img"`），
以及生成器追加的 `Restart` / `Power off` 两个 action。

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
- `[[block_slice]]`、`[[driver]]`、`[acpi]`、kit 当前未声明：无对应负载/表/驱动文件，保持生成器默认。
