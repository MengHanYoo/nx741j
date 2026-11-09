# 八个移植步骤本地核对（nx741j）

依据官方[移植设备 · 八步](https://unixnu.com/zh/porting/steps)。设备仓库 revision `50c3d14`（2026-11-09）。

| 步骤 | 官方结果要求 | 本地状态 | 证据 / 缺什么 |
|---|---|---|---|
| 1 收集设备输入 | 硬件清单与缺失输入列表 | ✅ | `HARDWARE-INVENTORY.md`（SoC/CPU/GIC/TLMM/PMIC/UFS/面板/WCN/固件版本）、`STEP1-HOST.md`、`DTB-PARAMETERS.md`；仍缺 dtbo 触摸节点（非标准 `dt_table`）与 WCN board-id |
| 2 选择共享支持 | 已选仓库与精确版本 | ✅ | `feeds.toml`（sm8850 `825a5304`）+ `unixnu.lock`（9 个源）；见 `SHARED-SUPPORT-SELECTION.md` |
| 3 创建设备仓库 | 格式 2、源码固定、参数明确 | 🟡 | `schema_version = 2`、`board.example` 已移除、布局文件齐（board/feeds/porting/AGENTS/docs/configs/dts/package/scripts，113 个受控文件）；**缺 `.github/workflows/porting-record.yml`** |
| 4 解析并检查构建配置 | `defconfig/fetch/status/tree` 通过 | ✅ | 9 组件 / 24 模块 / 67 补丁；`scripts/porting-record.py check`：14 个结构化区域通过 |
| 5 准备硬件描述和固件 | 可构建固件 + 两个树输入 | ✅ | 4 MiB FD（DXE/BDS、无 LogDumpDxe）、菜单、payload/ADT、bootimg；硬件 DTB 5,614 B → 56,798 B（本轮加入 mdss/DSI 与面板） |
| 6 按依赖顺序适配驱动 | 每项能力有配置与验证 | 🟡 | 平台 ✅（ArmDTPlatform/ArmCpu/SoftwareAES）；存储 🟡（构建通过，运行时 fail-closed）；显示 🟡（GopFramebuffer 已选 + 控制器 DT 入库，DPU/DSI/背光驱动未写）；输入、GPU、WCN、基带、音频 ⬜ |
| 7 构建并启动 | 构建输出 + 设备观测 | 🟡 | 层 1–3 ✅；层 4（菜单→DarwinBoot）断点未定位；层 5 未启动 |
| 8 完成并发布 | 格式 2 + unixnu topic + 设备页 | 🟡 | 检查器与 38 篇文档齐；缺 CI workflow、GitHub `unixnu` topic、官网设备页；最新 1 个提交（`50c3d14`）未推送 |

## 本轮变化（2026-11-09 下午）

- 步骤 5/6：显示设备树入库 —— `dts/nx741j-mdss.dtsi`（33 实节点 + 19 个 disabled 占位 provider）由
  `scripts/mk-mdss-dtsi.py` 从 live FDT 生成，面板挂 `&dsi0`；dtc 编译通过，ADT 仍 12,972 B 且内容一致。
  细节见 `DISPLAY-DT.md`。
- 组件 6 → 9（`VirtioMmioBlock`、`VerifiedRoot`、`ShengUfs` 入列），模块 20 → 24，补丁 63 → 67。
- 步骤 8 的缺口之一已补：`board.example` 移除，构建不再拒绝示例仓库。

## 存疑

- Advanced Logger 的采集模块需要把 DxeCore 的调试通道切到 `AdvancedLoggerLib`；当前屏幕日志依赖
  `FrameBufferSerialPortLib`，切换会改变可观测性，故先驻留导出器、保留现有通道。
- 触摸/无线型号已确认（Synaptics TCM、WCN7880）；未确认的是 dtbo 里的触摸节点与 WCN board-id。
- 构建树 `/opt/work/nx741j-build` 的 `out/` 与设备副本归 root，非 root 会话无法跑完整 `payload`；
  设备仓库以 `E:\default-workspace\z80u-collect\repos\UniXNU-nx741j` 为准。
