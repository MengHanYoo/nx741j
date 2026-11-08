# 步骤 5 · 硬件描述与固件集成

依据 [设备树](https://unixnu.com/zh/porting/device-tree)、
[UEFI 固件 · 必需集成](https://unixnu.com/zh/porting/uefi-firmware#required-integration) 与
[日志抓取 · 固件设置](https://unixnu.com/zh/porting/log-capture#firmware-setup)。

## 1. 设备树与 ADT

| 项 | 状态 | 说明 |
|---|---|---|
| 板级 DTS | ✅ | `model`/`compatible`（nubia,nx741j + qcom,alor/sm8850）、单一连续 memory（`0x8C0000000+5 GiB`）、8 个 CPU + `timebase-frequency`、`/psci method="smc"`、GICv3、timer |
| GIC 导出 | ✅ 本轮修复 | 板级覆盖 `&intc { unixnu,adt-name = "intc"; }`，否则 ADT 的 `/arm-io` 无中断控制器 |
| timer 导出 | ✅ 本轮修复 | `timer { unixnu,adt-name = "arm-timer"; }` |
| 生成 ADT 必需节点 | ✅ | `make-adt.py` 重新生成后包含 `arm-io`、`chosen`、`cpus/cpu0..7`、`defaults`、`options`、`product`、`memory-map`、`intc`、`arm-timer`、`random-seed`、`unique-chip-id`、`chip-id`、NVRAM proxy |
| 独立转换 | ✅ | `dtc` + `make-adt.py --unique-chip-id 098EE0C3 --data-ramdisk --trust-cache` 成功（12,148 B ADT） |
| reserved-memory | ✅ 本轮补入板级 DTS：ramoops、gunyah hyp/heap、cpucp_pdp、xbl_dtlog、aop image/cmd-db、smem、qdss、dsm1/2、mpss、ipa_fw、splash（取自实机 DTB）；`dtc` 编译通过，ADT 仍只导出带 `unixnu,adt-name` 的节点 |

## 2. 固件必需集成

| 模块/库 | 状态 |
|---|---|
| `UniXNU.inf` / `UniXNUDxe.inf` / `DarwinBoot.inf` | ✅ DSC + FDF（DarwinBoot 经 APRIORI 先于 BdsDxe） |
| `UniXNURamDiskDxe.inf` | ✅ DSC + DXE.inc（日志可见 `UNIXNURAMDISK: initrd block device`） |
| `DiskIoDxe` / `Fat.inf` / `PartitionDxe` | ✅ DXE.inc |
| `FdtLib` / `HobLib` | ✅ 本轮补 `HobLib|DxeHobLib`，`FdtLib` 已有 |
| FD 路径/大小 | ✅ `Build/nx741jPkg/{target}_CLANGPDB/FV/SILICIUM_UEFI.fd`，FD_BASE `0xff300000`，FD_SIZE `0x400000` |
| 预检 | ✅ `kits/nx741j/firmware-preflight.py` 应用 overlay + vendored UniXNUDxe |

本轮修复：新增 `AdvLoggerInstallDxe` 时与 FDF 显式行重复导致 `GenFv: same file GUID`，已改为只经
`DXE.inc` 引入；FD 重新构建成功。

## 3. 固件日志（log-capture#firmware-setup）

| 项 | 状态 |
|---|---|
| `AdvancedLogDxe.inf` | ✅ DSC + FDF |
| `AdvancedLoggerAccessLib` / `QrEncoderLib` | ✅ DSC 库映射 |
| `PcdUniXNUDeviceModel` | ✅ `"nx741j"`（导出文件名） |
| `[payload] tools = true` | ✅ Tools → Advanced Logger 菜单 |
| 日志协议 | ✅ 改用平台现有 `AdvLoggerPkg/AdvancedSerialLogger/Dxe/AdvancedSerialLoggerDxe.inf`（真实安装 `gAdvancedLoggerProtocolGuid` 并接串口）；自写空 installer 已删除 |
| 日志内容 | ✅ DxeCore 实例会在无 HOB 时分配 log buffer 并安装协议；插件可读到本次固件日志（串口/内存两种后端） |

## 4. 构建产物（本轮）

| 产物 | sha256 |
|---|---|
| `payload.img` | `cfb56751b42a54762517dfeaf933f5bc45fc772c3330d4948ad7286333f2a89c` |
| `boot.img` | `f7b91e14cfe4b7f180f9d25d113271dd8c73a3f68a9745c886190bad50d1ac6b` |
| 固件 FD | `c7cafaa672f33f0e50980c4418e3180fdbfe501009c8cb9c46cf7accc9b5274b` |

## 5. 存疑

| 项 | 说明 |
|---|---|
| reserved-memory | 未进 DTS；如需 XNU 识别保留区，需要 `unixnu,adt-name` 或 ADT 生成器支持 |
| Advanced Logger 内容 | 协议已安装，但缓冲来自平台收集模块；当前无 HOB 时为空 |
| FD 余量 | ✅ 预检已加入 **FDF/DXE.inc 重复 INF 检查**，重复 GUID 在任何编译前直接报错；4 MiB 上限仍在，新增模块需先确认 FV 能放下 |
| 实机验证 | ❌ 无法离线完成：需刷 V68 FD（`c7cafaa6…`）让 XNU 真正启动，当前层 4 仍卡在菜单/交接 |
