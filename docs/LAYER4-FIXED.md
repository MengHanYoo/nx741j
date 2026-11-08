# 层 4（菜单 → DarwinBoot）修复记录

设备实测链路：V68 → V71 → V75，最终出现 `DARWINBOOT: entry called` 与
`DARWINBOOT: Boot called`，层 4 打通。

## 根因与修复

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | BDS 不加载 UniXNU，直接 MemoryDump | `UniXNUDxe` 写 `Boot####` 失败（本机变量服务不可用），BDS 无启动项 | V68：写变量失败时直接 `LoadImage`/`StartImage` UniXNU |
| 2 | 菜单出现，倒计时后 MemoryDump | XNU 条目调用 `gRT->SetVariable`（同一变量服务问题） | V71：XNU 条目跳过 UEFI 变量写入 |
| 3 | 探针停在各处后 MemoryDump | 调试用的 10–20 秒 `Stall` 触发 BDS 看门狗 | V75：移除所有长停顿（≤2 s） |
| 4 | `loader locate -> Success` 后崩 | 同上的长停顿 | 改为打印指针/字段、短停顿 |
| 5 | kext 阶段崩溃 | 空 kext 配置仍调用 `UniXNUStageKexts` | V75：`KextCount == 0` 时跳过 |
| 6 | `UniXNULog` 首次调用后崩（疑似） | 日志服务在 XNU 条目路径不可用 | V75：先 DEBUG/Print，`UniXNULog` 仅保留在 handoff 处 |

## 实机证据

```
UNIXNU: menu returned index 0
UNIXNU: entry selected / entry ptr
UNIXNU: BootEntry entered
UNIXNU: first log done
UNIXNU: XNU entry, variables skipped
UNIXNU: device tree prepared -> Success
UNIXNU: after device tree
UNIXNU: RunXnu entered
UNIXNU: looking for the loader protocol
UNIXNU: loader locate -> Success
UNIXNU: loader fields ok
UNIXNU: no kexts configured
UNIXNU: handing off, kexts 0
UNIXNU: calling Loader->Boot
DARWINBOOT: entry called
DARWINBOOT: Boot called, req … fdt … ramdisk …
```

## 复现

`package/upstream/UniXNUPkg/Library/UniXNULib/{Main.c,Boot.c}` 与
`Driver/UniXNUDxe/UniXNUDxe.c` 由 `package/firmware-preflight.py` 覆盖进构建树；
`unixnu build firmware` 即得到相同行为（FD 需重新打包，payload 不变）。

## 下一步

DarwinBoot 已接管，需继续确认：内核解析/trust cache/ADT、EL1 跳转、XNU 早期日志。

## V76 / V77（DarwinBoot 内部）

- V76：UniXNU 对 XNU 条目不再传 DeviceTree（改用 payload 的 `.adt` sidecar），DarwinBoot 内部
  10 s Stall 降为 1 s；实机已见 `DARWINBOOT: kernel … bytes` 与 `DARWINBOOT: adt … window …`。
- V77：在 DarwinBoot 的 video/window/copy/map/ExitBootServices 之间加入打印，FD
  `d49993ff2e914ae95dacbf271dc4d432a881d511ad9d4f4c71dd83294b7db2f7`；待下次 9008 验证。
