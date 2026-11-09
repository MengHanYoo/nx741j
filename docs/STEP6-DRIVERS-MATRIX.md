# 步骤 6 · 驱动适配矩阵（nx741j）

依据官方[适配驱动](https://unixnu.com/zh/porting/drivers)的 8 级硬件依赖顺序与构建接入规则。
设备仓库 revision `c1b229907e4b8d9aff1db6223bd73d36fb0ecde3`。

## 8 级顺序与本地状态

| 顺序 | 能力 | 官方组件 | 本机状态 | 说明 |
|---|---|---|---|---|
| 1 | CPU/中断/定时器/RAM/DMA | ArmDTPlatform、ArmCpu、ArmSmmu4K | 🟡 部分 | 前两者已选并按 arch profile 归档；ArmSmmu4K 是 kit（无归档），**SMMU 节点已进设备树**（`nx741j-hw.dtsi` 的 `apps-smmu@15000000`，status ok，phandle 与 stock 一致）；平台输入见 `STEP6-PLATFORM.md` |
| 2 | 存储/根卷/持久 Data | ShengUfs、RamBlock、VirtioMmioBlock、VerifiedRoot | 🟡 构建通过 | 已选中并链接（`libShengUfs.a` 含 ABI 参考对象 + 真实 VerifiedRoot）；UFS 节点已导出到 ADT（`ufshc`）；运行时仍 fail-closed（无策略包） |
| 3 | 帧缓冲/刷新/亮度 | GopFramebuffer、DisplayTimingProbe、AppleARMBacklight、GeniI2c | 🟡 部分 | 面板 `bf375`+`rm692h5`、GOP 1216×2688 已确认；仅 `GopFramebuffer` 已选；MSM 显示控制器的 DT 已入库（`DISPLAY-DT.md`），驱动未实现 |
| 4 | 触控与按键 | QcomGeniSpi、ShengHidInput、ButtonProbe（+自建 Synaptics TCM） | ⬜ 未开始 | 触控为 Synaptics TCM（`syna_firmware.img` 已提取），`ShengNovatekTouch` 不适用；UEFI 按键仍受 TLMM 映射阻塞 |
| 5 | GPU/Metal | Adreno740、AdrenoMetal、Sm8550Platform、AdrenoFirmware、AdrenoPM4、AirNir | ⬜ 未开始 | 实机 DTB `Adreno840v2`（gen8-2-1，chipid `0x44050a01`）；目录 A740 需新实现 |
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

## 本轮修复（按驱动顺序的配置/构建层）

| 问题 | 影响 | 修复 |
|---|---|---|
| `dts/nx741j.dts` 的 `nx741j-hw.dtsi` include 落在注释块内 | UFS/PCIe/cnss/dwc3/GPU/SPI 触摸节点**从未进入编译后的 DTB** | include 移到注释块之后；`hardware.dtb` 现含 7 个驱动节点（ufshc、pcie、cnss-peach、dwc3、kgsl、spi/synaptics_tcm 等） |
| UFS 节点 `status="disabled"` 且无 ADT 名 | 存储驱动拿不到 `/arm-io/ufshc` | 改为 `status="okay"` + `unixnu,adt-name="ufshc"`；ADT 重新生成后包含 `ufshc` |
| 驱动矩阵状态过时 | 存储仍标 🔴 | 更新为 🟡（已选中并链接，运行时 fail-closed）；平台 ✅ |

验证：

```sh
./unixnu payload   # dtc 通过；hardware.dtb 含驱动节点
make-adt.py …      # /arm-io 内含 intc、arm-timer、ufshc
```

产物：payload `7ac7776f…`、boot `14596c1c…`、FD `c7cafaa6…`。

## 本轮·显示设备树（2026-11-09 下午）

第 3 级的前置输入从"面板参数"推进到"整条显示控制器的绑定"：

| 问题 | 影响 | 修复 |
|---|---|---|
| 显示节点只提取到 `_tmp`，没有进仓库 | `&dsi0` 无定义，面板文件从未编译过 | 新增 `dts/nx741j-mdss.dtsi`（33 实节点 + 19 占位 provider），生成器 `scripts/mk-mdss-dtsi.py` 一并入库 |
| phandle 替换器扫全文 `<0x…>` | 混进 LUT 表里的数值，且只映射到 56% | 改为属性白名单 + 按 provider `#*-cells` 走查；只改 `clocks/interconnects/power-domains/iommus/nvmem-cells/*-supply/*-gpio(s)/pinctrl-*/connectors` 等真引用 |
| 面板文件 `nx741j-panel-bf375.dtsi` 缺 2 个闭合括号 | 一旦被 include 就是语法错误（此前没接线所以没暴露） | 补齐 `panel@0` 与 `&dsi0` 的收尾；文件接进 `nx741j.dts` |
| 面板引用 `<0x33 …>`/`<0x7b4>`、`phandle = <0x7e8>` | 编译后是悬空数字 | 改成 `&tlmm`、`&dsi_panel_pwr_supply`，删掉 `phandle`；面板节点加 `panel` 标签供 `qcom,dsi-default-panel` 引用 |

验证（与 `devicetree.py` 相同的 dtc 命令与 include 路径）：硬件 DTB 5,614 B → 56,798 B，
无 error；`make-adt.py` 用旧/新 DTB 生成的 ADT 都是 12,972 B、内容一致（显示节点不带
`unixnu,adt-name`，不进 `/arm-io`）。详见 `DISPLAY-DT.md`。

`./unixnu payload` 未在本机跑完：构建树的 `out/`、`dts/` 归 root，本会话的 WSL 用户没有
免密 sudo；需要 root 以 `-D /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j`
重跑一次。

## 第 1–4 级的组件实况（2026-11-09）

官方 drivers 页列出的实现与本仓库 catalog（`components.toml`，9 项已选）对不上，先把差距写清楚：

| 级别 | 官方列的实现 | catalog 实况 | 结论 |
|---|---|---|---|
| 1 平台/内存 | ArmDTPlatform、ArmCpu、ArmSmmu4K、SoC DTSI | 前两者已选；**ArmSmmu4K 是 `kind = "kit"`、无 `build`/归档**；SoC DTSI 为 `UniXNU-sm8850-common` | ArmSmmu4K 不是可选项；缺口在设备树，已补 `apps-smmu@15000000`（`nx741j-hw.dtsi`） |
| 2 存储/根卷 | ShengUfs、RamBlock、VirtioMmioBlock、VerifiedRoot | 三项已选，**RamBlock 未选**（`kind = "driver"`，`build-arm-driver.py` → `libRamBlock.a`） | 策略包已能生成/编入；运行时缺信封投递与 xnu-8796 补丁 |
| 3 显示/亮度 | GopFramebuffer、DisplayTimingProbe、AppleARMBacklight、Geni2c | 只有 **GopFramebuffer**；其余三个**不在 catalog** | 亮度/时序只能自己写或等上游组件；显示驱动（DPU/DSI）需自建 |
| 4 输入 | QcomGeniSpi、ShengNovatekTouch、ShengHidInput、ButtonProbe | QcomGeniSpi、ShengHidInput 存在；**ShengHidInput 依赖 ShengNovatekTouch**（本机是 Synaptics TCM）；**ButtonProbe 不在 catalog** | 触摸要新建 `SynapticsTcm` 组件，不能套 Novatek 通路 |

因此第 3、4 级"按官方顺序操作"目前只能做到：DT 就绪 + 选已存在的组件；真正缺的是新驱动代码。

第 3 级还查到一件会影响"启动到 iOS"的事实：`GopFramebuffer` 的 `NativeMobileDisplay` 通过
`PE_current_console(&video)` 取 UEFI 交接的 framebuffer（**不读设备树**，所以 DT 里的 `framebuffer`
节点只是文档），而它只实现了少数 ABI 入口，`unclassified168/169/170/171` 直接 `panic()`。
用户态一旦调用这些入口就会宕机——这是 UI 阶段必须补的，不是配置问题。

## 仍未实现的驱动（需要源码，不是配置）

| 顺序 | 能力 | 现状 | 需要的输入 |
|---|---|---|---|
| 3 | 显示/背光 | 仅 GopFramebuffer | DPU/DSI + backlight 组件 |
| 4 | 触摸/按键 | 无驱动（keys 探针已禁用） | Synaptics TCM 协议实现、TLMM 可映射方案 |
| 5 | GPU/Metal | 无 | Adreno840v2 新实现（目录仅 A740） |
| 6 | Wi-Fi/BT/USB | 无 | WCN7880 适配、dwc3 组件 |
| 6a | 基带 | 无 | QcomBaseband 输入 |
| 7 | DSP/音频/电源 | 无 | WSA884x/ADSP 组件 |

这些属于第六步的“实现驱动”部分，需逐个子系统提交源码与实机验证；当前仓库已把它们的 DTB 节点、固件、参数全部准备好。
