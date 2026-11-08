# 八个移植步骤本地核对（nx741j）

依据官方[移植设备 · 八步](https://unixnu.com/zh/porting/steps)。设备仓库 revision `c6e53ee0bac67644de7a8daa2bec00594fef1c1f`。

| 步骤 | 官方结果要求 | 本地状态 | 证据 / 下一步 |
|---|---|---|---|
| 1 收集设备输入 | 硬件清单与缺失输入列表 | ✅ 已完成 | `docs/HARDWARE-INVENTORY.md`（SoC/CPU/GIC/TLMM/PMIC/UFS/面板/固件版本；触摸、无线模组仍标注待确认） |
| 2 选择共享支持 | 已选仓库与精确版本 | ✅ 已完成 | `docs/SHARED-SUPPORT-SELECTION.md` + `unixnu.lock`（sm8850 `825a5304`，组件 revision 已按 `status --json` 校正） |
| 3 创建设备仓库 | 源码固定、硬件参数明确的仓库 | ✅ 已完成 | 官方 16 项布局齐全；`board.example` 缺失；`configs/nx741j_defconfig`、`dts/nx741j.dts` 与实际文件一致 |
| 4 解析并检查构建配置 | 依赖解析完成 | ✅ 已完成 | `defconfig/fetch/status/tree` 通过；components 6、modules 20、patches 63；检查器通过 |
| 5 准备硬件描述和固件 | 可构建固件 + 两个树输入 | ✅ 固件可构建；日志导出已驻留 | 必需模块（UniXNU/UniXNUDxe/DarwinBoot/RamDisk/DiskIo/FAT）在卷；`AdvancedLogDxe` 已加入 DSC/FDF（协议缺失时驻留）；payload 生成 `kernel.macho.adt`，bootimg 用固件 DTB |
| 6 按依赖顺序适配驱动 | 每项能力有配置与验证 | 🔴 未开始（平台/内存之外） | 先 UFS（需核对 SM8850），再显示/输入；见 `docs/COMPONENT-SELECTION.md` 计划项 |
| 7 构建并启动 | 构建输出与设备观测 | 🟡 部分 | 层 1–3 已验证；层 4 菜单倒计时后 MemoryDump（`DARWINBOOT: Boot called` 未出现） |
| 8 完成并发布仓库 | 格式 2、可检索 | 🟡 部分 | 检查器通过；`unixnu` topic 与 CI 通过待发布 |

## 本轮按官方文档新增的配置

- 固件 overlay 收录 DSC/FDF/APRIORI/DXE/驱动/SEC/KeyCallback（可复现构建）；
- `PcdUniXNUDeviceModel = "nx741j"`，`AdvancedLoggerAccessLib` 与 `QrEncoderLib` 已映射，
  `AdvancedLogDxe` 进入 DSC/FDF（Depex 等待 `gAdvancedLoggerProtocolGuid`，不改变现有调试通道）；
- 启动配置、组件选择、本地开发、步骤 1–4 记录均在 `docs/`。

## 存疑

- Advanced Logger 的采集模块需要把 DxeCore 的调试通道切到 AdvancedLoggerLib；当前屏幕日志依赖
  `FrameBufferSerialPortLib`，切换会改变可观测性，故先驻留导出器、保留现有通道；
- 触摸/无线模组型号需从 stock 固件或 teardown 确认后回填硬件清单。
