# 启动配置（reference/boot-configuration）对照分析

依据 `https://unixnu.com/zh/reference/boot-configuration`；对象是 payload 卷里实际打包的
`/EFI/UniXNU/unixnu.toml`（设备）与 QEMU ESP 里那份（测试）。

## 1. 设备侧实际内容（623 B）

```toml
version = 1
[boot]  timeout=5 rotate=0 fallback_timeout=10 connect_all=true default="xnu"
[ui]    title="NX741J" scale=0 logo=true layout="auto" portrait_below=800 console="auto"
[kits]  autoload=false
[[entry]] xnu      type="xnu"    path="Payloads/kernel.macho" args="keepsyms=1 kextlog=0xffff native-ufs-probe=14 native-ufs-diagnostics=5 native-ufs-dma-offset=0 -v serial=3"
[[entry]] reset    type="action" action="reset"
[[entry]] shutdown type="action" action="shutdown"
[[entry]] tools    type="action" action="tools"
```

## 2. 逐节对照

| 官方段 | 文档键 | 我们 | 说明 |
|---|---|---|---|
| `[boot]` | timeout/rotate/fallback_timeout/connect_all/default | ✅ 5 键全用 | `default="xnu"` 由生成器固定 |
| `[ui]` | title/scale/logo/layout/portrait_below/console | ✅ 6 键全用 | 1216×2688 比值 452 `< 800` → 竖屏单列布局 |
| `[[driver]]` | path/args/connect/enabled | ❌ 不用 | `loader="firmware"`，DarwinBoot 由 FV 驱动提供 |
| `[[dt_patch]]` | op/node/prop/value/type/file | ❌ 不用 | xnu 条目不带 DT，设备树走 ADT sidecar（`Boot.c:90` 置 NULL） |
| `[[block_slice]]` | partition/name/offset/length/writable/enabled | ⬜ 生成器已支持、尚无条目 | 阶段 6 发布 SHNGROOT/OS/SystemOS 窗口时填 |
| `[acpi]` | add=[...] | ❌ 不用 | arm64 XNU 不消费 ACPI；`AcpiTableDxe` 只在 EFI/Linux 路径有意义 |
| kit（`[[kit]]`+`[kits]`） | path/enabled；autoload/directory | ⚠️ 仅 `[kits] autoload=false` | 设备侧不自动加载 kit；QEMU ESP 用 `autoload=true` 加载 DarwinBoot.kit |
| `[[entry]]` | id/title/type/path/args/dtb/initrd/confirm/confirm_titles/confirm_args/action/enabled | ✅ 4 条（xnu + 3 个 action） | 未用 `dtb`/`initrd`（无 Data 种子）/`confirm*`/`enabled` |
| `[entry.chosen]` | 任意键 → /chosen 属性 | ❌ 不用 | 命令行走 `args`，ADT 由生成器写 |
| `[[entry.kext]]` | bundle/plist/executable/enabled | ❌ 不用 | kext 预链接进内核，不走菜单项注入 |
| `[[entry.variable]]` | guid/name/value/type/volatile | ❌ 不用 | 不使用 UEFI 变量注入 |

## 3. 生成器覆盖面

`scripts/unixnu/payload.py` 覆盖 6 个表：`[boot]`、`[ui]`、`[kits]`、`[[driver]]`（仅当 `loader != "firmware"`）、
`[[block_slice]]`（2026-11-11 新增 `render_block_slices()`）、`[[entry]]`。
其余 6 个表（`[[dt_patch]]`/`[acpi]`/`[[kit]]`/`[entry.chosen]`/`[[entry.kext]]`/`[[entry.variable]]`）没有 `board.toml` 键，
只能手写进 `unixnu.toml` 或改生成器。

## 4. 菜单行为（我们的实际路径）

| 条件 | 官方 | 我们 |
|---|---|---|
| 有输入设备 | 倒计时到 `timeout` | 本机固件**无输入设备**（TLMM 按键阻塞），走下一行 |
| 无输入设备 | 使用 `fallback_timeout` | ✅ 10 秒后自动启动 xnu |
| 启动失败 | 返回菜单，不再倒计时 | ✅ DarwinBoot 失败返回（Fail 屏重绘版除外） |

输入接通后可把 `timeout` 改 `-1`（等待按键），这是 `[boot]` 唯一的计划变更。

## 5. 路径与校验

- `path = "Payloads/kernel.macho"` 相对负载卷根 ✅；`initrd` 缺省（无 Data 种子）✅；
- 校验方式与文档一致：`mtype -i payload.img ::/EFI/UniXNU/unixnu.toml`（已执行）；
- 修改启动参数的流程一致：改 `[payload] kernel_args` → `./unixnu payload` → `./unixnu bootimg`。

## 6. 结论

1. 设备侧配置是文档 schema 的**合法子集**：`[boot]`/`[ui]`/`[kits]`/`[[entry]]` 四段，全部键值合规；
2. 未用的 7 段里，**只有 `[[block_slice]]` 是阶段 6 真正需要的**（映射已就绪）；ACPI/dt_patch/driver/kext/variable 对 iOS 路径不适用；
3. 唯一计划中的变更：输入接通后 `timeout=-1`；
4. 文档缺项（对我们无影响）：`[payload]` 下的 `rotate`/`fallback_timeout`/`connect_all`/`[payload.ui]`、`bootimg.dtb` 未列入 configuration 页。
