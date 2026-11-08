# 步骤 6 · 驱动适配矩阵（nx741j）

依据官方[适配驱动](https://unixnu.com/zh/porting/drivers)的 8 级硬件依赖顺序与构建接入规则。
设备仓库 revision `c1b229907e4b8d9aff1db6223bd73d36fb0ecde3`。

## 8 级顺序与本地状态

| 顺序 | 能力 | 官方组件 | 本机状态 | 说明 |
|---|---|---|---|---|
| 1 | CPU/中断/定时器/RAM/DMA | ArmDTPlatform、ArmCpu、ArmSmmu4K | 🟡 部分 | 前两者已选并按 arch profile 归档；ArmSmmu4K 未选；平台输入见 `STEP6-PLATFORM.md` |
| 2 | 存储/根卷/持久 Data | ShengUfs、RamBlock、VirtioMmioBlock、VerifiedRoot | 🔴 阻塞 | UFS 硬件清单已完成；`gcc-canoe` 偏移待从 vendor 模块提取 |
| 3 | 帧缓冲/刷新/亮度 | GopFramebuffer、DisplayTimingProbe、AppleARMBacklight、GeniI2c | 🟡 部分 | 仅 `GopFramebuffer`（嵌入式阶段） |
| 4 | 触控与按键 | QcomGeniSpi、ShengNovatekTouch、ShengHidInput、ButtonProbe | ⬜ 未开始 | 面板/触摸型号待确认；UEFI 按键另有 TLMM 映射阻塞 |
| 5 | GPU/Metal | Adreno740、AdrenoMetal、Sm8550Platform、AdrenoFirmware、AdrenoPM4、AirNir | ⬜ 未开始 | 目录实现面向 SM8550/A740 |
| 6 | Wi-Fi/蓝牙/USB | Ath12kWifi、WifiProbe、WifiNet、MhiBoot、QcaBluetooth、QcomDwc3 | ⬜ 未开始 | 模组型号待确认 |
| 6a | 基带 | QcomBaseband、modem/QRTR/IPA | ⬜ 未开始 | 目录参考 SC7180 |
| 7 | DSP/音频/电池/调频/电源 | AdspProbe、AudioProbe、ShengBattery、CpuFreqProbe、ShengPlatformPower | ⬜ 未开始 | 排在存储/显示之后 |
| 8 | OS 服务与打包 | SoftwareAES、AppleKeyStoreState、XnuPrelink、DarwinBoot、ShengPreflight | 🟡 部分 | SoftwareAES/XnuPrelink/DarwinBoot 已选；AppleKeyStoreState/ShengPreflight 未选 |

## 构建接入（官方第 3 节）

| 规则 | 本仓库现状 |
|---|---|
| 组件 ID 加入完整 `[components] include` | 6 个，见 `board.toml` |
| 归档按依赖顺序用 `component:<ID>` | arch profile 的 `[kernel].archives` 已列 `component:ArmDTPlatform`、`component:ArmCpu`、`component:SoftwareAES`；设备层不重复声明（数组替换语义） |
| 归档开关 | `profiles/arch/arm64/configs/common.config` 已设 `CONFIG_XNU_BUILD_ARCHIVES=y` |
| 驱动开关 | defconfig 中 `# CONFIG_XNU_NATIVE_UFS is not set`、`# CONFIG_XNU_NATIVE_ADRENO is not set`，对应当前未选组件 |
| 板级代码位置 | `package/`（构建树中为 `kits/nx741j`） |
| 初始化调用 | 由各驱动 probe/start 或板级脚本显式调用；当前仅在固件层有 `package/firmware-overlay/` |

验证命令（官方）：

```sh
./unixnu status --json -D ../UniXNU-nx741j
./unixnu kernel -D ../UniXNU-nx741j
llvm-ar t <archive.a>; llvm-nm --defined-only <archive.a>
./unixnu payload -D ../UniXNU-nx741j
./unixnu bootimg -D ../UniXNU-nx741j
```

## 存疑 / 缺项

| 项 | 说明 |
|---|---|
| UFS GCC 偏移 | stock 内核是 GKI，`clk-canoe` 在 vendor 模块；需从 `/vendor/lib/modules` 取 `.ko` 分析 |
| 触摸/无线型号 | 需 stock 固件或拆机确认 |
| `Sm8550Platform` | 目录按 SM8550 命名；SM8850 共享 SoC 支持是否可复用待核对 |
| 设备侧验证 | 层 4（菜单→DarwinBoot）未通，所有驱动只能做到源码/主机级 |

## 下一步

1. 取到 `clk-canoe.ko` → 提取 UFS 分支/复位偏移 → 适配 `ShengUfs` → 选中组件并验证存储；
2. 存储打通后按顺序做显示（`AppleARMBacklight`/`GeniI2c`）与输入（`QcomGeniSpi`/触摸）。
