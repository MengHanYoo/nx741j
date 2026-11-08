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
| reserved-memory | ⚠️ | 实机 51 段（836.6 MiB）已记录在 `DTB-PARAMETERS.md`，但未写入板级 DTS；XNU ADT 只输出导出节点，暂不影响阶段 5 |

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
| 日志协议 | ✅ 新增 `nx741jPkg/Drivers/AdvLoggerInstallDxe` 链接 `AdvancedLoggerLib`，安装 `gAdvancedLoggerProtocolGuid` |
| 日志内容 | ⚠️ 平台无 SEC/PEI Advanced Logger HOB 时缓冲区为空，插件可能显示 `Advanced Logger: no log`；需后续接入真实平台收集模块 |

## 4. 构建产物（本轮）

| 产物 | sha256 |
|---|---|
| `payload.img` | `467b3bcfdf08131c32f40597410fb766251d6d3d8aa5101fef017062a13907b4` |
| `boot.img` | `0ab6a94e106194bada023aeea033981cf19cdfb4058f4ef80e5e43a611b6ba36` |
| 固件 FD | `bbbcc3cf1b5dfe648a45544f171e2ff70425534c067772f4570b477ea1d42b0a` |

## 5. 存疑

| 项 | 说明 |
|---|---|
| reserved-memory | 未进 DTS；如需 XNU 识别保留区，需要 `unixnu,adt-name` 或 ADT 生成器支持 |
| Advanced Logger 内容 | 协议已安装，但缓冲来自平台收集模块；当前无 HOB 时为空 |
| FD 余量 | 4 MiB 上限，新增模块需先检查 FV 大小；重复 GUID 会直接令 GenFv 失败 |
| 实机验证 | 阶段 5 需 XNU 启动后才算通过（当前层 4 未过） |
