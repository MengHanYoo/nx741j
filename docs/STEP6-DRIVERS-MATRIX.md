# 步骤 6 · 驱动适配矩阵（nx741j）

依据官方[适配驱动](https://unixnu.com/zh/porting/drivers)的 8 级硬件依赖顺序与构建接入规则。
设备仓库 revision `c1b229907e4b8d9aff1db6223bd73d36fb0ecde3`。

## 8 级顺序与本地状态

| 顺序 | 能力 | 官方组件 | 本机状态 | 说明 |
|---|---|---|---|---|
| 1 | CPU/中断/定时器/RAM/DMA | ArmDTPlatform、ArmCpu、ArmSmmu4K | 🟡 部分 | 前两者已选并按 arch profile 归档；ArmSmmu4K 未选；平台输入见 `STEP6-PLATFORM.md` |
| 2 | 存储/根卷/持久 Data | ShengUfs、RamBlock、VirtioMmioBlock、VerifiedRoot | 🔴 阻塞 | UFS 型号/容量/LUN、`[userdata]`、`gcc-canoe` 分支偏移均已取得；`ShengUfs` 源码覆盖就绪，链接缺 storage ABI 归档 |
| 3 | 帧缓冲/刷新/亮度 | GopFramebuffer、DisplayTimingProbe、AppleARMBacklight、GeniI2c | 🟡 部分 | 面板 `bf375`+`rm692h5`、GOP 1216×2688 已确认；仅 `GopFramebuffer` 已选 |
| 4 | 触控与按键 | QcomGeniSpi、ShengHidInput、ButtonProbe（+自建 Synaptics TCM） | ⬜ 未开始 | 触控为 Synaptics TCM（`syna_firmware.img` 已提取），`ShengNovatekTouch` 不适用；UEFI 按键仍受 TLMM 映射阻塞 |
| 5 | GPU/Metal | Adreno740、AdrenoMetal、Sm8550Platform、AdrenoFirmware、AdrenoPM4、AirNir | ⬜ 未开始 | 目录实现面向 SM8550/A740 |
| 6 | Wi-Fi/蓝牙/USB | Ath12kWifi、WifiProbe、WifiNet、MhiBoot、QcaBluetooth、QcomDwc3 | ⬜ 未开始 | 实机 WCN7880：BT 固件 BRAHMA/GANGES、PEACH BDWLAN、MAC `5C:7D:AE:43:D9:F8` 已取；组件按 WCN7850 编写需适配 |
| 6a | 基带 | QcomBaseband、modem/QRTR/IPA | ⬜ 未开始 | 目录参考 SC7180 |
| 7 | DSP/音频/电池/调频/电源 | AdspProbe、AudioProbe、ShengBattery、CpuFreqProbe、ShengPlatformPower | ⬜ 未开始 | codec WSA884x（`mixer_paths_canoe_mtp_wsa884x`）已识别；排在存储/显示之后 |
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

## 驱动接口定义（官方第 1 节）

对已选 6 个组件按官方五类接口登记；来源为组件目录字段、源码与本次构建产物。

| 组件 | 硬件 | 传输 | 内存 | OS | 构建 |
|---|---|---|---|---|---|
| `ArmDTPlatform` | ADT 根节点/子设备/NVRAM；无 MMIO | device-tree 解析 | 由 XNU 分配 | `IOService` 平台专家，匹配 `arm-io` | `drivers/ArmDTPlatform/ArmDTPlatform.cpp` → `libArmDTPlatform.a` |
| `ArmCpu` | GICv3 dist/redist（`0x17000000+0x10000`、`0x17080000+0x200000`，stride `0x40000`）；19.2 MHz 定时器 | 无请求队列；CPU 启动/IPI 走 PSCI | 每 CPU 栈由内核分配 | CPU 注册、`arm,cpu` 匹配、IPI | `ArmCpu.cpp` → `libArmCpu.a` |
| `SoftwareAES` | 无独立 MMIO（走 IOCryptoAcceleratorFamily） | 同步 AES-CBC 调用 | 调用方缓冲 | `IOAESAccelerator` ABI 绑定 | `NativeAES.cpp` + bindings → `libSoftwareAES.a` |
| `GopFramebuffer` | UEFI GOP 帧缓冲（1216×2688，32bpp） | 无 | 帧缓冲映射 | `IOService` 显示 provider | `NativeMobileDisplay.cpp` → `libGopFramebuffer.a` |
| `DarwinBoot` | 无 MMIO；内存窗口由 boot_args 描述 | 文件读取 + Mach-O/ADT 校验 | `WINDOW_SIZE` 连续窗口 | UEFI driver，产出 `EFI_UNIXNU_LOADER_PROTOCOL` | `DarwinBoot.c` → FV 内 `DarwinBoot.efi` |
| `XnuPrelink` | 无 | 命令行工具 | 无 | 预链接脚本 | `kits/XnuPrelink/emit_prelink.py` |

### 资源所有权与清理（官方第 3.2 节）

| 组件 | 所有者 | 释放路径 |
|---|---|---|
| `ArmCpu` | 内核（CPU 启动后常驻） | 无卸载路径；失败时停止后续 CPU 启动 |
| `ArmDTPlatform` | 内核平台专家 | `deleteList`/`free`（源码内置） |
| `SoftwareAES` | 内核加密服务 | 实例 detach 时释放请求缓冲 |
| `GopFramebuffer` | 内核显示服务 | `stop` 时解除映射 |
| `DarwinBoot` | 固件（`ExitBootServices` 前） | `Unload` 卸载协议 |

### 本次构建验证

```sh
./unixnu kernel -D ../UniXNU-nx741j      # 归档 + 预链接成功，kernel.macho sha256 1d38517f…
llvm-ar t build/armdt-platform/libArmDTPlatform.a   # ArmDTPlatform.o
llvm-nm --defined-only build/armcpu/libArmCpu.a     # ArmCpu 元类/方法符号
llvm-ar t build/software-aes/libSoftwareAES.a       # NativeAES.o + original-bindings.o
```

设备仓库 revision `4d28c6089e0fc4e60c973278747b3f5ba142a646`。
