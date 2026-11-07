# 阶段 2 官方条目逐项核对（修正版）

依据：官方 `concepts/boot-chain` 阶段 2 与 `reference/boot-configuration` 的
`unixnu.toml` 段定义。核对对象为 2026-11-07 的实机结果（V18/V20）与源码。

| # | 官方要求 | 状态 | 证据 / 修正说明 |
|---|---|---|---|
| 1 | 固件卷包含 UniXNU 启动管理器和它的驱动 | ✅ 已验证 | V15 最小 FV（32 驱动）实机出菜单；`UniXNU.efi`、`UniXNUDxe`、`UniXNURamDiskDxe`、`DarwinBoot`、`KernelStub` 在卷内 |
| 2 | `UniXNUDxe` 注册 `Boot####`，load option 数据 `unixnu:firmware` | ✅ 已验证（源码） | `UniXNUDxe.c`：`BuildLoadOption()` 写入 `Description="UniXNU"` + 设备路径 + `unixnu:firmware`，`SetVariable("Boot####")` 后把该项插到 `BootOrder` 首位（第 202/228 行）。V20 增加 `UniXNUDxe: Boot#### registered, option N bytes` 直接日志。**旧表把它标 ⚠️ 是错的** |
| 3 | BDS 启动该应用；用户也可从固件菜单启动 | ✅ / ⚠️ | BDS 启动已验证（菜单出现）；"从固件 Boot Manager 选择"需要实体键，属输入设备问题，不是 BDS 问题 |
| 4 | 扫描启动卷与其他卷，寻找 `\EFI\UniXNU\unixnu.toml` | ✅ 已验证 | 菜单出现 `unixnu` 条目、标题 `NX741J`，说明 `unixnu.toml` 被解析。**旧表 ❌ 是错的** |
| 5 | 发布每个 `[[block_slice]]` | N/A | 官方语义：按 GPT 分区名发布块切片。当前 payload 配置 **0 个** `[[block_slice]]`，且本机固件没有可用的 GPT 磁盘（无 UFS BlockIo），因此无对象可发布。功能由 UniXNU 提供，未禁用 |
| 6 | 加载并启动每个 `[[driver]]` | N/A | 官方语义：加载 payload 里的 UEFI 驱动（`path` 或 `fv:<GUID>`）。当前配置 **0 个** `[[driver]]`；`[payload] loader = "firmware"`，DarwinBoot 由固件卷提供（阶段 3 使用）。**修正**：旧表称"依赖 payload 配置"，准确说法是"当前未使用该段" |
| 7 | 安装 ACPI 表 | N/A | 官方语义：`[acpi] add = [...]` 通过 `EFI_ACPI_TABLE_PROTOCOL` 逐个安装。当前配置无 `[acpi]` 段；该协议由固件提供（`AcpiTableDxe` 需在 FV，当前最小卷未含）。**待办**：如需 ACPI，先加 `AcpiTableDxe` 再补 `[acpi]` |
| 8 | 合并 `[match]` 条件满足的 kit | N/A | 官方语义：`[[kit]]` 显式加载，或 `[kits] autoload=true` 扫描 `directory`；`[match]` 键全匹配才生效。当前 `[kits] autoload=false` 且无 kit 目录。**修正**：旧表称"依赖 payload/kit 与配置"，准确说法是"未启用" |
| 9 | 构造菜单 | ✅ 已验证 | 4 个条目（`xnu`/`reset`/`shutdown`/`tools`）绘制正常，标题取自 `[ui] title` |

## 存疑项结论

| 存疑项 | 结论 |
|---|---|
| 是否真的走 `Boot####` 启动 | 源码已确认写入 `Boot####` + `BootOrder`；菜单条目即该启动项的执行结果。V20 日志给出直接证据 |
| 无输入设备时菜单行为 | 官方 `[boot] fallback_timeout`（默认 10 s）就是为这种情况设计的；当前正是走该路径。**因此"菜单无按键"不影响阶段 2 判定**，但为了可用性仍需补输入驱动 |
| `[[block_slice]]`/`[[driver]]`/ACPI/kit 是否"未实现" | 否。它们是**按配置启用**的可选步骤；当前配置未声明，属 N/A，不是缺陷 |

## 剩余真实缺项

1. 实体按键输入（UEFI 侧）：厂商 `ButtonsDxe` 依赖已裁剪的 PMIC/TLMM/DTBExtn 链；按 DTS 自研
   `Nx741jKeysDxe`（VOL+=GPIO101、FOCUS=179、SHOOT=95，active-low，TLMM `0x0F000000`）；
2. 若要启用 ACPI 表：先在最小 FV 补回 `AcpiTableDxe`，再在 payload 配置加 `[acpi]`；
3. 若要验证 `[[block_slice]]`/`[[driver]]`/kit：需要可用的块设备或对应文件，当前不作为阻塞项。
