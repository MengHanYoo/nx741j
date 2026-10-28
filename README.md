# NX741J — 在努比亚 Z80 Ultra 上原生运行 iOS 16.7

在**努比亚 Z80 Ultra（NX741J，骁龙 8 Elite Gen 5 / SM8850，Kaanapali）**上，
用自己编译的 **xnu-8796** 内核启动 iPhone X 的 **iOS 16.7**。

> 本项目从 [ShueiTsuki/data-a-live](https://github.com/ShueiTsuki/data-a-live)（小米 17 Pro / pandora）
> fork 而来，**所有板级参数一律以 Z80 Ultra 自己的设备树、cmd-db 与固件为准**，不照搬 17 Pro。
> 实验性研究项目，不是能日常使用的系统。

**仓库里只有补丁、驱动和工具，没有苹果的系统文件，也没有手机固件。**

## 现在能做什么

| 部分 | 状态 |
|---|---|
| 启动 | 写入 `boot_a` 的 loader 镜像；iOS 根文件系统放在安卓数据分区里的一个**钉住文件**中 |
| 锁屏 / 主屏幕 | 能进，**2–5 分钟**（带已建好 LaunchServices 库的镜像） |
| 触摸 | 能用（Synaptics TCM over SPI，供电 `L4B_E0` / `L14B_E0`） |
| 屏幕 | **Z80 原生 1216×2688、stride 4864、434 ppi**，1:1 无缩放 |
| 电源键 / 音量键 | 能用 |
| GPU（Adreno 840） | **实验阶段**：内核驱动、Vulkan（Mesa Turnip）、Metal（UniXNU NativeMetal）已在手机内跑通，GPU 提交能完成（实测 600+ fences、DCVS 222–1050 MHz）；**画面正确性与最终合成仍有问题**（壁纸纯色、相册闪退、快速滑动异常） |
| Wi-Fi / 蜂窝 / 声音 / 相机 / 蓝牙 | 不能 |
| 地图、健康等系统 App | 缺（使用精简系统根，完整 App 在 IPSW 的 App Cryptex 里） |

进度细节见 [docs/NX741J-PLAYBOOK.md](docs/NX741J-PLAYBOOK.md)，
最近一次阶段报告见 [docs/NX741J-REPORT-2026-10-29.md](docs/NX741J-REPORT-2026-10-29.md)。

## Z80 Ultra 实测参数（全部来自本机 DT / cmd-db / 固件）

| 项 | 值 | 来源 |
|---|---|---|
| SoC | SM8850（Kaanapali），8 核 | `/proc/cpuinfo`、DT |
| GPU | Adreno 840，`qcom,adreno-gpu-gen8-2-0`，chipid `0x44050a00` | live DT `qcom,kgsl-3d0@3d00000` |
| GPU 窗口 | kgsl `0x3d00000/0x40000`、rscc `0x3d50000/0x10000`、cx_dbgc `0x3d61000/0x3000`、cx_misc `0x3d9e000/0x2000` | 同上 `reg` |
| GMU | `0x3d37000/0x68000` + `0x3d40000/0x10000` | `qcom,gmu@3d37000` |
| GPU 中断 | SPI 300（kgsl）、80（cx_host）、304/305（HFI/GMU） | DT `interrupts` |
| GPU OPP | **17 档 160–1050 MHz**，初始 222 MHz；GMU 475 / 700 MHz | DT `qcom,gpu-pwrlevel@N`、`qcom,gmu-freq-table` |
| GPU 固件 | `gen80200_gmu.bin`、`gen80200_sqe.fw`、`gen80200_zap.mbn`、`gen80200_aqe.fw` | `/vendor/firmware` |
| 面板 | 1216×2688（DSC 命令模式），stride 4864 | DTBO `qcom,mdss_dsi_bf375_rm692h5_6p8_dev0_dsc_cmd` |
| 触摸 | Synaptics TCM，SPI `spi@1a80000`（SE19，CS0，10 MHz），IRQ GPIO 158，RESET GPIO 66，SPI GPIO 48–51 | DTBO `nx741j_dtbo026.dts` |
| 触摸供电 | **`L4B_E0`（1.8 V）+ `L14B_E0`（3.2 V）** | cmd-db 实测（`pmh0101_l4` / `pmh0101_l14`） |

> 17 Pro 的 `L4I_E0` 在 Z80 上**不存在**（投票返回 ENODEV），触摸 IC 不会上电。
> GPU 频点上限也是 **1050 MHz**，不是 17 Pro 的 1200 MHz。

## 目录

| 目录 | 内容 |
|---|---|
| `xnu/patches/` | 对 xnu-8796 的补丁，按编号顺序应用；`pandora` / `nx741j` 开头的是本机专用 |
| `wsl/` | 编进内核的驱动源码（GPU、触摸、按键、存储、固件包安装等），以及生成补丁 / 本地编译的脚本 |
| `shim/` | 启动小程序：`loader` 代替 iBoot 把内核拉起来 |
| `tools/` | 启动镜像、设备树、固件包、信任缓存生成工具；`gen_z80_gpu_tables.py` 从手机 DT 生成 GPU 频率表；GPU 测试程序（vktest、pgputest、mtltest） |
| `tools/turnip/`、`tools/metal/` | 在 GitHub Actions 上为 iOS 16 交叉编译 Turnip 与 NativeMetal 插件 |
| `docs/` | 移植手册、阶段报告、GPU 对照笔记 |
| `device/` | 只放说明文档；设备导出的原始数据不进仓库 |
| `.github/workflows/` | 内核（`xnu.yml`）与 GPU 用户态（`turnip.yml`、`metal.yml`）的自动编译 |

## 内置的关键驱动

| 驱动 | 位置 | 说明 |
|---|---|---|
| GPU（Adreno 840） | `wsl/pandora_gpu.c`、`iokit/Kernel/arm/PandoraAdrenoGpu.cpp` | 15 级分级上电；GMU 冷启动 + HFI；CP / ring / fence；用户客户端（`0x4147`） |
| 显示 | `iokit/Kernel/arm/SweetFramebuffer.cpp`、`wsl/pandora_dpu.c` | 面板 1216×2688；以 CPU 拷屏为主，`pandora_scanout=1` 可走硬件扫描 |
| 触摸 | `wsl/pandora_touch.c` | Synaptics TCM；供电候选、SPI 引脚自适配、cmd-db 名字转储 |
| 存储 | `wsl/sweet_ufs.c` | 只认钉住文件占用的块，不越界 |
| 固件包 | `wsl/mkfwpack.py` 与内核侧安装 | `PANDFW01` 包，把 GPU 固件 / 插件装进系统 |

## 启动方式

```bash
# 1. 根镜像写进安卓数据分区的钉住文件
adb push iosroot.img /data/local/tmp/iosroot.img
adb shell su -c '/data/local/tmp/pinfile create /data/local/tmp/iosroot.img 16384'

# 2. 刷 loader 镜像（覆盖 boot_a；恢复见下）
adb shell su -c 'dd if=/data/local/tmp/loader-boot.img of=/dev/block/by-name/boot_a bs=1M conv=fsync'
adb reboot
```

恢复：长按电源 → 9008 模式 → 用工具刷回原版 `boot_a.img`。

## 关键启动参数

```
unentitled_ios_sim_launch=1 ignition_level=1 launchd_unsecure_cache=1
wdt=-1 wdt_test=no_user_checkin
sweetfb_hz=30 sweetfb_fill=0 pandora_scanout=0
pandora_gpu=15 pandora_gpu_metal=1
pandora_touch_gpio117=0
```

- `pandora_gpu=15`：GPU 最高级（含用户客户端），14 级不够
- `pandora_gpu_metal=1`：发布 `IOAcceleratorES` 服务，Metal 才会去找插件
- `ignition_level=1`：提高会去挂载不存在的 Preboot 卷
- `sweetfb_fill=0`：1:1 呈现（`=1` 是最近邻放大，会让文字变马赛克）

## 移植中踩过的关键坑（供后来者参考）

1. **卡 logo 的根因是 MobileGestalt 缓存缺失**：容器目录在、缓存 plist 不在 →
   lsd 全部线程等信号量 → LaunchServices 建库永远完不成。补一个
   `CacheVersion=1` 的 `com.apple.MobileGestalt.plist` 即可。
2. **不要用本地旧镜像覆盖数据分区**：iOS 每次启动都会写入库与缓存，
   覆盖会退回「重建库」状态（15 分钟 + 死锁风险）。成功启动一次后把镜像拉回来当基准。
3. **GPU 插件要三样东西**：`pandora_gpu=15`、`NativeAdrenoMetal.clients`（内容 `backboardd`）、
   Turnip 已安装**且其 cdhash 在信任缓存里**。缺最后一项时报的是 `LoaderNotFound`，不是路径错。
4. **信任缓存的 cdhash 只能对 CodeDirectory 本身求哈希**（按其 length 字段）。
5. **本地重链接必须显式列出所有改过的文件**：漏传 `pandora_touch.c` 会退回 CI 旧对象，
   触摸供电轨变回 17 Pro 的 `L4I_E0`，触摸立即失效。
6. **BO 池的 extent 上限会被分配 / 释放循环打满**：`GPU_POOL_EXTENTS` 512 → 1024 后，
   `kIOReturnNoMemory` 与 `GPU HANG` 同时消失。

## 和 ios-sweet、UniXNU 的关系

- **ios-sweet 一系**：沿用它在骁龙手机上原生跑 iOS 16 的做法，驱动直接编进 xnu，
  以补丁形式维护（`xnu/patches/`）。前面一部分补丁来自 ios-sweet 与
  [ios-fuxi](https://github.com/bangprovn/ios-fuxi)。
- **UniXNU**：本项目**不是** UniXNU 的机型仓库。借用了 UniXNU 的 GPU 组件：
  NativeAdreno 的用户接口、ArmSmmu4K、NativeMetal 插件与配套 Turnip 补丁，
  并按 UniXNU 的贡献指南带了 `unixnu` 话题。

## 安全

- 不刷写手机分区以外的数据，不清数据；所有修改都可恢复。
- iOS 只读写它自己那个钉住文件：存储驱动只认这个文件占用的块；
  不加 `ufsrw=1` 时完全只读。
- 仓库永远不放：苹果的系统文件（IPSW、dyld 共享缓存、SDK）、高通和努比亚的固件、
  手机分区备份、设备导出和日志。`.gitignore` 已挡住这些目录。
- 自己动手前，先完整备份手机；出现意外后果需要自己承担。

## 致谢

- [ios-sweet](https://github.com/dipa-studio/ios-sweet)：在骁龙手机上原生跑 iOS 16 的开创项目
- [ios-fuxi](https://github.com/bangprovn/ios-fuxi)：ios-sweet 在骁龙 8 Gen 2 上的移植
- [ShueiTsuki/data-a-live](https://github.com/ShueiTsuki/data-a-live)：本仓库的上游（小米 17 Pro）
- [UniXNU](https://github.com/UniXNU/UniXNU)：K 页内核、存储驱动，以及 NativeAdreno、NativeMetal、ArmSmmu4K
- [Mesa](https://gitlab.freedesktop.org/mesa/mesa)（Turnip Vulkan 驱动）、[metal2vulkan](https://github.com/steelbrain/metal2vulkan)
- Linux 内核的 msm/Adreno 驱动：GPU 寄存器与启动顺序的参考
