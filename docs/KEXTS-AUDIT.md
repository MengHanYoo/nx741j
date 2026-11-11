# kext 包（drivers/kexts）对照分析

依据 `https://unixnu.com/zh/drivers/kexts`；对照本机的驱动集成方式（预链接内核 + 组件归档）。

## 1. 文档给出的两条路径，官方默认是「预链接」

| 路径 | 文档描述 | 我们 |
|---|---|---|
| **预链接/内置驱动**（默认） | 「随项目提供的 DarwinBoot 启动预链接内核，驱动集成路径见内置 XNU 驱动」 | ✅ 正在用 |
| 暂存 kext（`[[entry.kext]]` / kit `[[kext]]`） | 需要 **loader revision 2**；文档明说「**发布版 DarwinBoot 接受内核、ADT、trust cache 和可选 ramdisk，拒绝携带暂存 kext 的请求**」 | ❌ 未用，也**不该用** |

结论：我们不使用 `[[entry.kext]]` 不是缺项，而是与官方默认路径一致；要用暂存 kext 必须先把加载器升级到 revision 2。

## 2. 我们的驱动集成方式

| 层 | 方式 | 证据 |
|---|---|---|
| 自带驱动（ShengUfs / ArmSmmu4K / VerifiedRoot / VirtioMmioBlock） | 编进内核：`[kernel] archives` 的 `component:*` + `build-*.py` | `logs/archive-*.log`、`kernel.macho` 里 `NativeUfs`/`verifed_root`/`ArmSmmu4K` 符号 |
| Apple 内置 kext | 预链接保留：`[kernel.prelink] roots` = `AppleARMPlatform`、`ApplePMGR`、`AppleImage4`、`apfs` | `board.toml` `[kernel.prelink]`、`kernel.json` 里的 `com.apple.*` |
| kext 暂存 | **不用** | payload 的 `unixnu.toml` 无 `[[entry.kext]]`；QEMU kit 的 `Kit.toml` 无 `[[kext]]` |

## 3. 如果将来要走暂存 kext，需要补什么

| 项 | 文档要求 | 我们的现状 |
|---|---|---|
| 加载器 revision | 实现 `UNIXNU_LOADER_KEXT`、接受 revision 2 请求 | DarwinBoot 为 revision 1（只收 kernel/ADT/TC/ramdisk） |
| 包布局 | `X.kext/Contents/{Info.plist,MacOS/X}`；plist 非空；可执行 thin AArch64 `MH_KEXT_BUNDLE`（filetype 11） | 无 |
| 注册 | `[[entry.kext]] bundle=...` 或 kit `[[kext]] bundle/entry/plist/executable` | 无 |
| 信任 | AMFI 按 trust cache + 代码签名校验；**kext 的 CD hash 必须进 trust cache 包** | 我们的 trust cache 是自制扁平包，只覆盖当前内核/AMFI 路径，不含任何 kext CD hash |
| ABI | 可执行文件必须匹配目标内核 ABI（文档示例是 `xnu-10063.141.1` / iOS 17） | 我们是 **xnu-8796 / iOS 16.7.16**，ABI 目标不同 |
| 构建 | Xcode + iOS kernel SDK（`MODULE_TYPE=KEXT`） | 无 macOS，**无法用文档给的方式构建 kext** |

## 4. 对当前阶段的影响

- 阶段 6/7 需要的驱动（UFS 存储、显示、输入）都可以走**预链接组件**路径，不需要 kext 暂存；
- 唯一可能逼我们改走 revision 2 的场景是「必须动态加载第三方 kext」（例如 GPU 用户态插件的内核侧），那要同时改 DarwinBoot 协议、扩展 trust cache、并解决 Xcode 构建环境；
- 目前不构成阻塞。

## 5. 结论

1. 官方的 kext 暂存路径**发布版 DarwinBoot 明确拒绝**，所以本机不用它是正确选择；
2. 我们的驱动集成（组件归档 + 4 个预链接 root）与「内置 XNU 驱动」文档一致；
3. 若将来需要动态 kext，缺口是四项：loader revision 2、包暂存校验、trust cache 收录 CD hash、Xcode/iOS SDK 构建链——建议到那一步再立项。
