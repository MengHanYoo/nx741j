# 层 4 探针固件（V67）

层 3→4 的断点未知：菜单倒计时后直接 MemoryDump，之前看不到任何 `UNIXNU:` 行。本版在
UniXNU 与 DarwinBoot 的关键步骤插入 DEBUG 轨迹与停顿，屏幕可读。

| 顺序 | 屏幕输出 | 停顿 |
|---|---|---|
| 1 | `UNIXNU: menu returned index N` | 15 s |
| 2 | `UNIXNU: entry variables set` | 10 s |
| 3 | `UNIXNU: device tree prepared -> status (fdt … size …)` | 10 s |
| 4 | `UNIXNU: looking for the loader protocol` / `loader locate -> status` | 10 s |
| 5 | `UNIXNU: calling Loader->Boot` | 10 s |
| 6 | `DARWINBOOT: entry called` / `existing loader -> status` | — |
| 7 | `DARWINBOOT: Boot called, req … fdt … ramdisk …` | 20 s |

判读：最后出现的那一行即崩溃/挂起点。若第 1 行都没有，问题在 `UniXNUMenuRun` 返回前；
若停在 5→6 之间，是 `Loader->Boot` 调用即崩；若到 7 后黑屏，进入 DarwinBoot 内部。
EOF

- 固件：`fv-bisect/fd-v67-layer4.img`（sha256 `764c388a…`）
- 证据：`UniXNU.efi` 与 `DarwinBoot.efi` 内已确认包含上述 ASCII/UTF-16 字符串。
- 源码修改仅存在于构建树的 `package/UniXNUPkg/Library/UniXNULib/` 与
  `drivers/DarwinBoot/`（临时调试，不入库）。
- 设备仓库 revision `8575d3d5ba777ae9ba862544679618672147585f`。
