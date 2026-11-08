# NX741J 任务 / 待办 / 问题归档（2026-11-08 快照）

> 本文替代 2026-11-06 版本。旧版的"卡在 RpmhDxe/TzDxeLA""阶段 3 未上机"等结论已被实机结果取代，
> 详细证据见 `STAGE2-CONFORMANCE.md`、`HARDWARE-INVENTORY.md` §5–6、`ANALYSIS-EFISP-VENDORBOOT.md` §20–24。

## 一、总体进度（按官方六层）

| 层 | 状态 | 说明 |
|---|---|---|
| 1 启动镜像 | ✅ | efisp loader（p5）替代 BootShim：读 rawdump 的 payload + FD，拷 FD 到 `0xFF300000`，关 MMU 跳入 SEC |
| 2 UEFI 固件 | ✅ | 32 驱动最小 FV；`SEC: TLMM MMIO HOB` 已发布；BDS 正常 |
| 3 UniXNU | ✅ | 菜单显示 `NX741J` 条目；payload 卷由 `UniXNURamDiskDxe` 从 x0 FDT 发布，FAT 挂载成功 |
| 4 DarwinBoot | ❌ | 菜单启动 `xnu` 后黑屏 → `ZTE MemoryDump Mode`（崩溃在内核之前）；已启用 `panic_screen` 仍无 panic 文本 |
| 5 内核 | ❌ 未开始 | 未观察到内核早期日志 |
| 6 用户态 | ❌ 未开始 | — |

## 二、已验证的关键结论

| 结论 | 证据 |
|---|---|
| `APRIORI DXE { INF }` 只定优先级，**不会**把模块加入 FV；必须同时在 `DXE.inc` 普通列表 | `FVMAIN.inf` 清单 + V22 前后字符串计数 |
| payload 卷链路成立 | `UNIXNURAMDISK: initrd block device 0xDFF7000 size 0x4000000 Success` + `Installed Fat filesystem` |
| 厂商 `PmicDxeLa` 在本入口下必崩 | `rpmh protocol is not initialized` → `ASSERT pm_rpmh.c:659`；RPMH 依赖 RSC/SCM |
| SCM 提供者与 TzDxeLA 是同一二进制 | `ScmDxeCompat.efi` 与 `TzDxeLA.efi` sha256 相同（`ecd7bdff…`）；恢复 SCM 即回到 `TzLoaderDxe.c:1150` 断言 |
| 平台内存映射表声明 TLMM 16 MiB | `MemoryMapLib.c`：`{"TLMM",0x0F000000,0x01000000,AddDev,MMAP_IO,...}`；但实测 `0x0F010000` 读取异常，`0x65000` 等待 V41 探针确认 |

## 三、待办（按优先级）

| # | 任务 | 前置 | 说明 |
|---|---|---|---|
| 1 | 阶段 4：定位 DarwinBoot/XNU 交接崩溃 | — | 在 `EFI_DARWIN_BOOT_REQUEST` 构造、ADT 解析、Mach-O 校验三处加日志；黑屏说明崩溃在打印前 |
| 2 | 按键（UEFI） | V41 探针结果 | 若 TLMM 可读 → 直接写死三键映射；不可读 → 改页表/映射层 |
| 3 | `DTBExtnProtocol` provider | — | 步骤 6 驱动适配前置；影响 `DALTLMM`/`PmicDxe`/`Ucx/Rng`/`HALIOMMU` |
| 4 | 音频/USB/基带驱动 | 步骤 6 | 输入已在 `HARDWARE-INVENTORY.md` §6 登记 |
| 5 | userdata 与根卷 | 阶段 4 通过后 | 官方 userdata 布局 |

## 四、已放弃的旧方案（保留记录避免重复）

| 方案 | 放弃原因 |
|---|---|
| 换根到 `uefi_a`（FD_BASE=0xC6800000） | 已由 efisp loader 路线替代，且 rawdump 方案可无损回滚 |
| 恢复 `TzDxeLA`/`ScmDxeCompat` | 同一二进制内 `TzLoaderDxe.c:1150` 断言，DEBUG 构建直接停机 |
| 恢复厂商 UFS/时钟/电源链 | 依赖 DTBExtn/HWIO/NPA/RPMH/SCM，本平台缺 provider |
| 在 DXE 用 `gDS->AddMemorySpace` 补 TLMM 映射 | 调用即崩（V34/V36/V37），AArch64 DxeCore 不接受动态 MMIO 映射 |

## 五、已知问题

| # | 问题 | 状态 |
|---|---|---|
| 1 | 阶段 4 黑屏 + MemoryDump | ❌ 待定位 |
| 2 | UEFI 实体按键 | ❌ 待 V41 结论 |
| 3 | `DTBExtnProtocol` 无 provider | ❌ 确认 Binaries 中不存在 |
| 4 | SMEM 数据内容读 0（协议可用） | 🟡 |
| 5 | WCN7850 IMEM board_id 未读 | 🟡 |
