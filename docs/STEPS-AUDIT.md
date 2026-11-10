# 八个移植步骤本地核对（nx741j）

依据官方[移植设备 · 八步](https://unixnu.com/zh/porting/steps)。设备仓库 revision `c948b72`（2026-11-10）。

> **闸门**：第 4/7 步的「通过」以**真跑 QEMU** 为准——`scripts/build-and-test.sh` 依次构建 kernel → payload → firmware，
> 再让 QEMU 启动真实 payload 并断言三个标记；缺一个、或 QEMU 根本没跑起来，脚本返回非零，**这一版不允许刷机**。
> 规则与判据见文末「QEMU 闸门」一节。

| 步骤 | 官方结果要求 | 本地状态 | 证据 / 缺什么 |
|---|---|---|---|
| 1 收集设备输入 | 硬件清单与缺失输入列表 | ✅ | `HARDWARE-INVENTORY.md`（SoC/CPU/GIC/TLMM/PMIC/UFS/面板/WCN/固件版本）、`STEP1-HOST.md`、`DTB-PARAMETERS.md`；仍缺 dtbo 触摸节点（非标准 `dt_table`）与 WCN board-id |
| 2 选择共享支持 | 已选仓库与精确版本 | ✅ | `feeds.toml`（sm8850 `825a5304`）+ `unixnu.lock`（9 个源）；见 `SHARED-SUPPORT-SELECTION.md` |
| 3 创建设备仓库 | 格式 2、源码固定、参数明确 | ✅ 本地 | `schema_version = 2`、`board.example` 已移除、布局文件齐（112 个受控文件）；记录路径改指 `dts/nx741j.dts`（原先指向模板残留 `dts/board.dts`），本地 `porting-record.py check` 通过；**按 owner 决定不启用 GitHub Actions**，仓库不带 `.github/workflows/` |
| 4 解析并检查构建配置 | `defconfig/fetch/status/tree` 通过，且构建结果在 QEMU 里真跑过 | ✅ | 10 组件 / 11 条归档 / 9 条设备补丁；`scripts/porting-record.py check`：14 个结构化区域通过；`scripts/build-and-test.sh` 每版真跑 QEMU（见文末） |
| 5 准备硬件描述和固件 | 可构建固件 + 两个树输入 | ✅ | 4 MiB FD（DXE/BDS、无 LogDumpDxe）、菜单、payload/ADT、bootimg；硬件 DTB 加入 mdss/DSI 与面板。2026-11-09 在 **ext4 构建树（16 线程）**跑通全链路：`./unixnu kernel` **158 s**（kernel.macho sha256 `36a618db…`）、`./unixnu payload` 通过（`adt_sha256 be90306b…`）、`check-bringup.py` **17/17** |
| 6 按依赖顺序适配驱动 | 每项能力有配置与验证 | 🟡 | 按官方 8 项能力逐条见下节「第 6 步明细」：1 平台 ✅、2 存储 🟡、3 显示 🟡（仅固件侧）、4–7 ⬜、8 🟡（3/5 已用） |
| 7 构建并启动 | 构建输出 + 设备观测 | 🟡 | 层 1–4 ✅（层 4 在 QEMU 三标记全绿并真机交接进内核）；层 5 已过 `PE_create_console`/`PE_init_platform`/`machine_startup`，停在 kext/控制台获取（见 `STEP5-BRINGUP.md`） |
| 8 完成并发布 | 格式 2 + unixnu topic + 设备页 | ⏸ 不发布 | owner 决定本地自用：仓库保持私有、不加 `unixnu` topic、不生成官网页面。记录本身已能渲染（中英文各 745 行），需要时再按下面的流程发布 |

## 本轮变化（2026-11-09 下午）

- 步骤 5/6：显示设备树入库 —— `dts/nx741j-mdss.dtsi`（33 实节点 + 19 个 disabled 占位 provider）由
  `scripts/mk-mdss-dtsi.py` 从 live FDT 生成，面板挂 `&dsi0`；dtc 编译通过，ADT 仍 12,972 B 且内容一致。
  细节见 `DISPLAY-DT.md`。
- 组件 6 → 9（`VirtioMmioBlock`、`VerifiedRoot`、`ShengUfs` 入列），模块 20 → 24，补丁 63 → 67。
- 步骤 8 的缺口之一已补：`board.example` 移除，构建不再拒绝示例仓库。
- 构建环境：`/opt/work` 归 root，复制到 `/mnt/e/unixnu-build/UniXNU` 后 `payload` 全链路通过（见 `LOCAL-DEVELOPMENT.md`）。

## 发布：不对外（本地自用）

本仓库按 owner 决定**不发布到 unixnu.com**：保持私有、不加 topic、不向 UniXNU 的 `devices.toml` 注册、不启用 GitHub Actions。
校验全部在本地完成：

```sh
python3 scripts/porting-record.py check --device E:/default-workspace/z80u-collect/repos/UniXNU-nx741j
scripts/ci-local.sh
```

设备页仍是"生成物"而非手写：UniXNU 的 `scripts/sync-devices.py` 会读默认分支的 `board.toml` + `porting.toml`
并渲染中英文页面。需要时可本地预览（不改远端）：

```sh
python3 - <<'PY'
# 用 UniXNU 的 porting-record.py 渲染本仓库记录
import importlib.util, subprocess
from pathlib import Path
tools_path = Path('<UniXNU>/scripts/porting-record.py')
spec = importlib.util.spec_from_file_location('porting_record', tools_path)
tools = importlib.util.module_from_spec(spec); spec.loader.exec_module(tools)
device = Path('E:/default-workspace/z80u-collect/repos/UniXNU-nx741j')
record = tools.check(device)
print(tools.render(record, 'MengHanYoo/nx741j', subprocess.run(['git','-C',str(device),'rev-parse','HEAD'],capture_output=True,text=True).stdout.strip(), zh=True))
PY
```

若要恢复发布，只需三步：公开仓库（或把默认分支切到设备树分支）→ `gh repo edit <repo> --add-topic unixnu` →
等 UniXNU 的定时同步生成页面；无需手写 Markdown。原始待办记录如下，供以后参考。

官方发现机制只看**公开**仓库的**默认分支**：`sync-devices.py` 按 `unixnu` topic / 名字描述搜索，读默认分支的
`board.toml` + `porting.toml`，二者有效才生成 `docs/devices/nx741j.md` 与 `docs/zh/devices/nx741j.md`
（页面正文就是 `porting.toml` 的渲染结果），并写回 `devices.toml`。

当前 GitHub 状态：

| 仓库 | 可见性 | 默认分支 | 说明 |
|---|---|---|---|
| `MengHanYoo/UniXNU-nx741j` | 私有（404） | main | 设备仓库 origin；发布应以它为准 |
| `MengHanYoo/nx741j` | 公开 | `main`（**已是设备树**，2026-11-09 合并） | `main` 与 `unixnu-nx741j` 指向同一提交，`board.toml`/`porting.toml` 都在默认分支上，具备被 `sync-devices` 发现的条件 |
| `MengHanYoo/UniXNU-sm8850-common` | 私有（404） | main | SoC 共享支持，不影响发现 |

要做：公开 `UniXNU-nx741j`（或把 `nx741j` 的默认分支切到设备树分支）→ 加 `unixnu` topic → 推送本次提交 →
等 UniXNU 的 `sync-devices` 定时任务（UTC 00:00/12:00）生成页面。

## 存疑

- Advanced Logger 的采集模块需要把 DxeCore 的调试通道切到 `AdvancedLoggerLib`；当前屏幕日志依赖
  `FrameBufferSerialPortLib`，切换会改变可观测性，故先驻留导出器、保留现有通道。
- 触摸/无线型号已确认（Synaptics TCM、WCN7880）；未确认的是 dtbo 里的触摸节点与 WCN board-id。
- 构建树 `/opt/work/nx741j-build` 的 `out/` 与设备副本归 root，非 root 会话无法跑完整 `payload`；
  设备仓库以 `E:\default-workspace\z80u-collect\repos\UniXNU-nx741j` 为准。

## QEMU 闸门（每版必过）

```sh
sudo bash scripts/build-and-test.sh ~/nx741j-build/UniXNU
```

四件事，任何一步失败即返回非零，这一版不允许刷机：

1. `./unixnu kernel`（增量）→ `./unixnu payload` → `./unixnu firmware`；
2. `scripts/qemu-nx741j.sh` 用真实 payload 组 ESP，起 `qemu-system-aarch64 -M virt`（40 s）；
3. 断言三个标记：`UNIXNU: BootEntry`、`MARK #29`、`QEMU: EBS ok, entering the kernel`；
4. 两道新鲜度检查，防止「QEMU 没跑起来、旧日志骗过闸门」：
   - runner 必须正常退出（提前退出 → `FAIL qemu runner exited …`）；
   - `serial.log` 必须含客户机固件横幅 `UEFI firmware (version`（只有真跑过才有）。
   `qemu-nx741j.sh` 在自身检查之前就清空 `serial.log`，提前退出只留下空文件。

判据：`Synchronous Exception` 出现在 `EBS ok` **之前**算失败（固件/DarwinBoot 的问题）；
出现在**之后**只记 NOTE（内核跑进 QEMU 的机器模型，不是加载器问题）。

最近一次真跑（2026-11-10 21:28，当前 payload）：横幅 1 次、三个标记各 1 次、`qemu exit: 124`（40 s 超时正常）；
`EBS ok` 之后 `Synchronous Exception 0x4B7FE52C` → NOTE。

反例验证：把 `serial.log` 换成空文件后，同一段判据输出 `FAIL  no guest firmware banner`，闸门按预期转红。

## 第 6 步明细（按官方 8 项能力）

| 顺序 | 能力 | 官方组件 | 本地 | 证据 / 缺口 |
|---|---|---|---|---|
| 1 | CPU、中断、定时器、RAM、DMA | ArmDTPlatform、ArmCpu、ArmSmmu4K、SoC DTSI | ✅ | `ArmDTPlatform`/`ArmCpu` 在 `[kernel] archives`；ADT 由实机 DTB 派生（5,332 节点）；`ArmSmmu4K` 没有独立归档，以 kit 形式由 `scripts/build-storage-bringup.py` 编进 `libShengUfs.a`（`ArmSmmu4K*.o` 7 个 + `SmmuBind.o`），内核里 `ArmSmmu4K`×29 / `board_smmu`×12 |
| 2 | 存储、根卷、持久 Data | ShengUfs、RamBlock、VirtioMmioBlock、VerifiedRoot | 🟡 | 内核里 `NativeUfs`×100、`verified_root`×3；`libShengUfs.a` 含 `NativeUfs/verified_root/xnu_crypto/Stubs/SmmuBind/ArmSmmu4K*`。运行时 fail-closed、无根卷；`RamBlock` 未选（payload 走的 `UniXNURamDiskDxe`）；缺 SM8850 clock/reset 表与信封投递 |
| 3 | 帧缓冲、刷新、亮度 | GopFramebuffer、DisplayTimingProbe、AppleARMBacklight、GeniI2c | 🟡 仅固件侧 | 固件 GOP 1216×2688 ✅；**`GopFramebuffer` 在 `[components]` 却不在 `[kernel] archives`，`kernel.macho` 里 0 命中**；`DisplayTimingProbe`/`AppleARMBacklight`/`GeniI2c` 本地目录里不存在 |
| 4 | 触控与实体按键 | QcomGeniSpi、ShengNovatekTouch、ShengHidInput、ButtonProbe | ⬜ | `QcomGeniSpi`/`ShengNovatekTouch`/`ShengHidInput` 在目录但未选，`ButtonProbe` 不在目录；**本机触控是 Synaptics TCM（SPI19 @ `1a80000`），目录里的参考实现是 Novatek → 需自写或改通用路径**；UEFI 按键另受 TLMM 阻塞 |
| 5 | GPU 与 Metal | Adreno740、AdrenoMetal、Sm8550Platform、AdrenoFirmware、AdrenoPM4、AirNir | ⬜ | 六个都在目录，面向 SM8550；SM8850 需对应实现 |
| 6 | Wi-Fi | Ath12kWifi、WifiProbe、WifiNet、MhiBoot | ⬜ | 只有 `Ath12kWifi` 在目录，其余三个不在；模组 WCN7850 |
| 7 | DSP、音频、电池、调频、电源 | AdspProbe、AudioProbe、ShengBattery、CpuFreqProbe、ShengPlatformPower | ⬜ | 五个**都不在本快照的目录里** |
| 8 | OS 服务与打包 | SoftwareAES、AppleKeyStoreState、XnuPrelink、DarwinBoot、ShengPreflight | 🟡 | `SoftwareAES`/`XnuPrelink`/`DarwinBoot` 已用；`AppleKeyStoreState`/`ShengPreflight` 在目录但未选 |

### 本次修正的三个错项

1. `SoftwareAES` 属第 8 项（OS 服务），此前被写进“平台”；
2. “输入阻塞在 TLMM”只描述 **UEFI 固件按键**；内核触控根本没开始（`porting.toml` 的 `input = planned`、`components = []`）；
3. “ArmSmmu4K 绑定”要写明它是 **kit**：没有独立归档，由 `build-storage-bringup.py` 编入 `libShengUfs.a`。

### 存疑（要决策或验证）

1. ~~`ShengUfs`、`VirtioMmioBlock` 的 `os_abi` 缺 `ios-16.7.16-20H392`~~ **已修复（2026-11-10 23:55）**：框架树 `components.toml` 的这两个条目已补上本机 profile，`./unixnu status --json` 解析确认，`porting-record.py check` 仍 14 区通过。**仍存疑**：`ShengUfs` 的 `hardware`/`board_dependencies`/`function` 仍写 SM8550（实现限制，先用板级补 SM8850 clock/reset 表），`verified_devices` 仍只有 `sheng`——os_abi 是 ABI 兼容声明，不等于本机验证过；
2. `porting.toml` 的 `display = implemented` 指固件侧能力，与“内核显示未实现”并存，读者会误判等级；
3. `storage = planned` 与“驱动已编进内核”并存，等级口径不统一；
4. 触摸 IC 是 Synaptics TCM 而参考实现是 Novatek：自写 TCM 驱动，还是走 `ShengHidInput` 的通用路径，未定。
