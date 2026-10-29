# NX741J（努比亚 Z80 Ultra）iOS 16.7 移植 — 最佳配置与复现手册

> 生成时间：2026-10-27 00:15
> 机型：**Nubia Z80 Ultra / NX741J / SM8850 (Kaanapali)**，slot `_a`，Android 17 底包
> 目标：在 Android 机身上启动 iOS 16.7.16 (20H392)，锁屏可用、触摸可用、GPU（Adreno 840）硬件全通

---

## 0. 一句话总结

**内核、显示、触摸、GPU 硬件（Adreno 840 全 14 级 + DCVS）已全部打通，且全部以 Z80 自己读到的参数为准**；
用户态 Metal 插件（`NativeAdrenoMetal.bundle`）已在本地用 Linux 工具链编译成功、签名、装入镜像并加入信任列表。
当前最佳配置 = `METAL-boot_a.img` + 含插件的 16 GB 根镜像。

---

## 1. 当前最佳配置（直接可复现）

| 项目 | 值 |
|---|---|
| 启动镜像 | `METAL-boot_a.img`（54,362,112 字节） |
| 内核 | 本地编译 `k4`（含触摸自适配补丁）→ prelink `k4-amfi.macho` |
| payload | `54,311,248` 字节，2 个信任模块（第二个 92 条，含插件） |
| 根镜像 | `iospull.img`（16 GiB，含 MobileGestalt 容器 + Metal 插件） |
| GPU 固件 | `iosfw.img`（2 MiB 钉住文件，`PANDFW01`，4 个 `gen80200_*`） |
| 屏幕 | 1216×2688，`pandora_scanout=1`（GPU 缓冲直接扫描） |
| GPU | `pandora_gpu=14`（全功能，DCVS 160–1200 MHz） |

### 完整启动命令行

```
debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress
rd=ufsroot0 ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1
wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1
sweetfb_hz=30 sweetfb_fill=1 pandora_scanout=1
pandora_touch_gpio117=0 pandora_gpu=14
sweetsample=lsd,SpringBoard
```

### 关键参数逐条理由

| 参数 | 值 | 理由 |
|---|---|---|
| `unentitled_ios_sim_launch=1` | 开 | 否则 iOS 二进制被判外星平台，launchd 被 SIGKILL（code 0xe） |
| `ignition_level=1` | **不要提高** | ≥2 会去挂载不存在的 Preboot 卷；1 才执行 dyld 缓存阶段 |
| `pandora_scanout=1` | 开 | 面板直接扫描 GPU 缓冲区，去掉 CPU 拷屏（`copy avg 3400 us`） |
| `pandora_gpu=14` | 14 | GPU 用户接口层；1–4 是分级探测，14 才让 Metal 可用 |
| `pandora_touch_gpio117=0` | 0 | GPIO117 是 17 Pro 的 avdd，Z80 上无关，必须不驱动 |
| `sweetnoexec` | **不设置** | 拦截服务会让同步等待它的进程永久阻塞 |
| `sweetsample=...` | 开 | 出问题时用内核采样栈定位（本次定位死锁的核心工具） |

---

## 2. 硬件实测参数（全部来自 Z80 本体）

### 2.1 显示

| 项 | 值 | 来源 |
|---|---|---|
| 面板 | `BF375_RM692H5_6P8_DEV0_DSC_CMD` | Android DT（dtbo_026） |
| 分辨率 | 1216 × 2688，stride 4864（4 B/px） | DT `hactive/vactive` |
| 刷新 | 60 / 120 Hz（DSC 命令模式） | DT |
| 亮度 | DSI 命令通路正常 | 内核日志 `SWEET DSI: brightness ... sent` |

### 2.2 触摸

| 项 | 值 |
|---|---|
| 控制器 | `synaptics,tcm-spi`（Synaptics TouchComm，SPI） |
| SPI 控制器 | `spi@1a80000`（SE19），父节点 `qcom,qupv3_4_geni_se@1ac0000` |
| 片选/频率 | CS0 / 10 MHz |
| 中断 | TLMM GPIO **158**，低有效边沿 |
| 复位 | TLMM GPIO **66** |
| SPI 引脚 | GPIO 48/49/50/51 |
| **VDD 1.8 V** | **`L4B_E0`**（cmd-db 实测名，`pmh0101_l4`） |
| **AVDD 3.2 V** | **`L14B_E0`**（`pmh0101_l14`） |
| 上电时序 | power 200 ms、reset-active 20 ms、reset-delay 200 ms |

> ⚠ 教训：早期用 17 Pro 的 `L4I_E0` 投票必然失败（返回 19/ENODEV），触摸 IC 根本不上电，
> SPI 引脚功能号 1..7 全部试不通（header 恒为 `00 00 00 00`）。

### 2.3 GPU（Adreno 840 / gen8_2_1）

| 项 | 值 |
|---|---|
| 芯片号 | `0x08020100`（上报给 Turnip） |
| GPU 基址 | `0x3d00000`（长 0x40000） |
| GMU | `0x3d37000`（长 0x68000），`gmu_ao_blk_dec0` 0x3d40000 |
| GPUCC | `0x3d90000`；`CX GDSCR 0x9080`、`gds_hw_ctrl 0x9094` |
| GX GDSC | `gxclkctl 0x3d64000 + 0x4024 = 0x3d68024` |
| PLL | 只有 PLL0（Taycan EKO T，950 MHz） |
| SMMU | `0x3da0000`，GPU SID 0/1，GMU SID 5，ACTLR `0x32b` |
| 固件 | `/vendor/firmware/gen80200_{gmu.bin,sqe.fw,zap.mbn,aqe.fw}` |
| cmd-db | `0x81c60000`，`gfx.lvl/mx.lvl/cx.lvl/gmxc.lvl/mxc.lvl/SH0/MC0/ACV/CN0` |

---

## 3. 启动链（efisp → boot_a）

```
xbl_a/b → abl_a/b → uefi_a/b → efisp(linuxloader_unlock.efi) → boot_a(loader-boot.img) → payload(SWEETXNU)
```

- `efisp` 已写入 `linuxloader_unlock.efi`，无需改动。
- `boot_a` 放我们的 `loader-boot.img`（mkbootimg 格式，内含 54 MB payload）。
- 恢复方式：长按电源 20–30 s → 9008 → 工具刷回原版 `boot_a.img`。

### payload 结构（mkpayload.py）

```
0x000 "SWEETXNU" | 0x008 version+nsegs | 0x010 virt_base/entry/adt_off/adt_size/kernel_end
0x038 phys_base/mem_size | 0x048 cmdline[608] | 0x2a8 段表
```

---

## 4. 根镜像（16 GiB 钉住文件）必须做的修改

### 4.1 MobileGestalt 缓存容器（**进锁屏的关键**）

缺失该容器会让 `lsd` 死锁，最终卡在苹果 logo：

```
/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/
    .com.apple.mobile_container_manager.metadata.plist   (397 B)
    Library/Caches/
```

元数据 plist 内容（binary plist）：

```python
import plistlib, uuid
d = {
 'MCMMetadataIdentifier': 'systemgroup.com.apple.mobilegestaltcache',
 'MCMMetadataInfo': {'com.apple.MobileGestalt.ContentProtectionClass': -1},
 'MCMMetadataVersion': 6,
 'MCMMetadataContentClass': 13,
 'MCMMetadataUUID': str(uuid.uuid4()).upper(),
 'MCMMetadataSchemaVersion': 1,
 'MCMMetadataActiveDPClass': -1,
}
plistlib.dump(d, open('mgcache_meta.plist','wb'), fmt=plistlib.FMT_BINARY)
```

属主 `root:root 0755`，`Library/Caches` 同。

> 死锁链：MG 缓存容器缺失 → libMobileGestalt 信号量永不返回 → lsd t1 卡住 →
> `restoreInactiveInstalls` 的 dispatch group 永不完成 → `performJournalRecovery` 的
> `dispatch_sync` 等不到队列 → LaunchServices 建库死锁 → SpringBoard 卡在 `LSApplicationRecord`。

### 4.2 迁移 / 设置向导 / 崩溃服务（已写入）

| 文件 | 作用 |
|---|---|
| `com.apple.migration.plist` + `ByHost/com.apple.migration.<UUID>.plist` | `LastSystemVersion=20H392`，跳过首次迁移 |
| `com.apple.purplebuddy*.plist` | 跳过 Hello 向导 |
| `com.apple.springboard.plist` | 禁看门狗、禁熄屏、禁自动暗屏 |
| `com.apple.managedconfiguration.notbackedup.plist` | 跳过云配置 UI |
| `/private/var/db/com.apple.xpc.launchd/disabled.plist` | 禁用 CommCenter / bluetoothd / fairplayd.H2 / driverkitd / sirittsd |

`IOPlatformUUID`（ByHost 文件名用）：

```
32884568-8A2C-5A2C-9FE8-E8FDA535909A
算法：SHA1(命名空间{A6DD4CCB B5E84AF5 ACDDB6DC 6A0542B8} + unique-chip-id[8] + chip-id[4])
      再把 digest[6] 高 4 位设 0x5、digest[8] 高 2 位设 0b10；两个 ID 都是小端
```

### 4.3 Metal 插件（本次新增）

```
/System/Library/Extensions/NativeAdrenoMetal.bundle/
    NativeAdrenoMetal    22,438,240 B  (ad-hoc 签名, ldid -S)
    Info.plist           CFBundleIdentifier=org.blu-sharky.NativeAdrenoMetal
                         NSPrincipalClass=NativeAdrenoMetalDevice
                         MinimumOSVersion=16.0
```

内核发布的匹配属性（`iokit/Kernel/arm/PandoraAdrenoGpu.cpp:619`）：

```c
setProperty("MetalPluginName", "NativeAdrenoMetal");
setProperty("MetalPluginClassName", "NativeAdrenoMetalDevice");
```

### 4.4 GU 固件包（钉住文件）

```bash
# 1) 从手机取固件（只读）
adb pull /vendor/firmware/gen80200_gmu.bin
adb pull /vendor/firmware/gen80200_sqe.fw
adb pull /vendor/firmware/gen80200_zap.mbn
adb pull /vendor/firmware/gen80200_aqe.fw

# 2) 打包（PANDFW01，4 KiB 头 + 文件）
python3 tools/mkfwpack.py /root/fwpack.img \
    gen80200_gmu.bin gen80200_sqe.fw gen80200_zap.mbn gen80200_aqe.fw

# 3) 手机上建钉住文件并写入
adb push fwpack.img /data/local/tmp/fwpack.img
adb shell su -c '/data/local/tmp/pinfile create /data/local/tmp/iosfw.img 2'
adb shell su -c 'cat /data/local/tmp/fwpack.img | /data/local/tmp/pinfile rawwrite /data/local/tmp/iosfw.img /dev/block/sda12 69 --yes'

# 4) 取 extents（ADT 用）
adb shell su -c '/data/local/tmp/pinfile extents /data/local/tmp/iosfw.img --json'
# → {"file": "...", "size": 2097152, "extents": [[0, 3001024512, 2097152, 4233]]}
```

内核按 `PANDFW01` 头查表读文件（`gpu_fw_file()`），ADT 里需要：

```
/chosen/pandora-fw-extents = [file_off, part_block, count] × N
/chosen/pandora-fw-blocks  = 总块数
```

---

## 5. payload 构建（完整命令）

```bash
P=/path/to/repos/nx741j
W=/path/to/z80u-collect

ADTFLAGS="--root-extents $W/iosroot_extents.json \
          --log-extents  $W/ioslog_extents.json \
          --fw-extents   $W/iosfw_extents.json"

# 信任缓存：第 1 个 = 静态（系统库），第 2 个 = AppOS 缓存 + 我们的插件
TRUSTCACHE="/opt/work/ipsw/Firmware/087-86698-044.dmg.trustcache \
            /opt/work/ipsw/Firmware/087-86593-047.dmg.trustcache+\
/opt/work/ipsw/Firmware/087-86715-047.dmg.trustcache+\
/opt/work/gpu/metal-out/plugin.trst"

EXTRA="ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 \
wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 \
sweetfb_hz=30 sweetfb_fill=1 pandora_scanout=1 pandora_touch_gpio117=0 \
pandora_gpu=14 sweetsample=lsd,SpringBoard"

D22AP_ADT=/opt/work/ipsw/d22ap.adt \
KERNEL=/opt/work/plk/k4-amfi.macho \
TRUSTCACHE="$TRUSTCACHE" \
ADTFLAGS="$ADTFLAGS" \
CPUS=8 RD=ufsroot0 DEBUG=0xc NAME=metalpl \
bash <(tr -d '\r' < $P/wsl/make_phone_payload_nx741j.sh) /opt/work/run1 "$EXTRA"
```

### 插件信任模块（raw 信任缓存，24 字节/项）

```python
import struct, hashlib
# cdhash = SHA256(CodeDirectory 字节) 前 20 字节
# 条目 = cdhash[20] + flags[4]（静态缓存标志 0x02000200）
mod = struct.pack('<I16sI', 2, hashlib.sha256(b'plugin').digest()[:16], 1) \
      + cdhash + bytes.fromhex('02000200')
```

本次插件 cdhash：`117aece1f54ea00c8fc706c100fb316289c16f1e`

---

## 6. 内核：本地编译流程（3–5 分钟一轮，不用 CI）

### 6.1 为什么能本地编

xnu 需要 macOS 的 clang/SDK；本地用 **cctools-port 的 Apple ld64**（ld64-956.6）+ CI 那一轮的
objects 重链接。已装好：

| 组件 | 路径 |
|---|---|
| Apple ld64 | `/opt/work/ld64/inst/bin/aarch64-apple-darwin-ld` |
| 编译日志 | `/opt/work/cilog/xnu-ci-54.full.log` |
| CI objects | `/opt/work/run2/{bt,ci}` |
| 源码树 | `/opt/work/tree` |
| 工具 | `/opt/work/tools/xnu-cc.py`、`/opt/work/sh/local_relink.py` |

### 6.2 一轮迭代

```bash
# 1) 改源码（例如触摸）
vi /opt/work/tree/bsd/dev/pandora_touch.c

# 2) 只重编改动的文件并重链接
bash /root/local_build.sh 2 /opt/work/outN bsd/dev/pandora_touch.c
#   → /opt/work/outN/kernel.development.pandora

# 3) prelink（把 kext 预链接进内核）
cd /opt/work/tools
python3 mkprelink.py /opt/work/outN/kernel.development.pandora \
    /opt/work/ipsw/kernelcache.arm64 /opt/work/plk/kN-amfi.macho

# 4) payload + loader（见第 5 节），然后 mkbootimg
```

`local_relink.py` 需要两处本地化修改（已打）：

```python
# (a) 保留 -L（fakeroot 里的 libfirehose_kernel.a）
elif a.startswith('-L'):
    ld.append(a.replace(xcc.CI_DXB, bt))
# (b) 强制拉取这几个模块符号
for sym in ('_sysctlbyname','_ubc_create_upl','_IOMallocData',
            '_IOMallocZeroData','_kalloc_type_impl'):
    ld += ['-u', sym]
```

> 注意：`ld64.lld` **不能**替代 Apple ld64 —— 它不实现 `-new_linker` 的 kalloc_type 合成。

### 6.3 触摸补丁（关键改动）

```c
/* bsd/dev/pandora_touch.c：默认供电轨改为 Z80 实测名 */
if (!PE_parse_boot_argn(key, arg, sizeof(arg))) {
    const char *d = i ? "L14B_E0" : "L4B_E0";
    ...
    mv = i ? 3200u : 1800u;
}

/* 引脚功能号自适配：1..7 依次试，读到合法 TouchComm 头即采用 */
for (unsigned f = mux_lo; f <= mux_hi; f++) {
    for (unsigned p = 48; p <= 51; p++) pt_gpio(p, 1, f, 6, -1);
    pt_sleep(50);
    err = pt_ic_start();
    printf("PANDORA TOUCH: mux %u -> %d\n", f, err);
    if (!err) break;
}

/* cmd-db 名字转储提前到进入延时之前（pandora_cmddb_dump=1 时） */
if (early_dump) { rh_db = ml_io_map(PT_CMDDB_PA, PT_CMDDB_SIZE); rh_dump_names(); }
```

---

## 7. Metal 插件：Linux 本地构建（macOS 脚本的等价物）

### 7.1 依赖

| 组件 | 版本/路径 |
|---|---|
| clang | 系统 clang 21 + `/usr/local/bin/clang-ios` 包装器 |
| iOS SDK | `/opt/work/gpu/sdk`（iPhoneOS 16.2） |
| 链接器 | `/usr/bin/ld64.lld`（支持 `.tbd`） |
| Rust | rustup + `aarch64-apple-ios` 目标 |
| 签名 | `ldid`（Linux 上替代 codesign） |
| 着色器 | `glslc` / `spirv-val` |

### 7.2 `clang-ios` 包装器

```bash
#!/bin/bash
SDK=/opt/work/gpu/sdk
exec /usr/bin/clang "$@" -fuse-ld=/usr/bin/ld64.lld -target arm64-apple-ios16.0 -isysroot "$SDK" \
  -Wl,-arch,arm64 -Wl,-platform_version,ios,16.0,16.0
```

### 7.3 源码仓库（全部固定 commit，来自 `turnip.yml` 的 env）

| 仓库 | commit |
|---|---|
| UniXNU | `1a01c2afa197d975d04da892325c56b6b14aaecb` |
| NativeAdreno | `b686b9054f57d393c80195b82568a09f8ca106a2` |
| NativeMetal | `c6d29caad6d880cc8204ebe3bc34d77479a3255d` |
| metal2vulkan | `720ef1ce9ae2a2068999a8675717418b9416283e` |
| Mesa | `85fda3b36405bcaea8bffc64aa4f2651dc90770d` |
| NativeMobileDisplay | `8666a9950c9699b368931d18cac92c0fdb2ce9c0` |

### 7.4 构建步骤（`wsl_build_plugin_local.sh` 摘要）

```bash
# 0) NativeMetal 适配 iOS 16
python3 tools/metal/adapt_ios16.py $NM

# 1) smoke shaders → gen/NativeTurnipShaders.h（glslc + spirv-val）

# 2) NativeMetal 的 0001..0007 补丁应用到 metal2vulkan

# 3) AIR fixtures
llvm-as -o runtime_examples.air  .../runtime_examples.ll
llvm-as -o function_constant.air .../kernel_function_constant_gate_fold.ll

# 4) Rust 桥（staticlib，**不需要链接器**）
cargo build --release --manifest-path build/Cargo.toml --target aarch64-apple-ios
#   → libnative_metal_bridge.a (308 MB)

# 5) bundle 编译
clang-ios -std=c11 -c kits/AdrenoPM4/AdrenoPM4.c -o AdrenoPM4.o
clang-ios -std=c++17 -fobjc-arc -bundle \
  -I $NA -I kits/AdrenoPM4 -I mesa/include -I build/gen \
  NativeAdrenoMetalPlugin.mm NativeBridgeMetalFrontend.mm AdrenoPM4.o \
  libnative_metal_bridge.a -Wl,-dead_strip -lobjc -lc++ -lc++abi \
  -framework Foundation -framework Metal -framework IOSurface \
  -o NativeAdrenoMetal.bundle/NativeAdrenoMetal

# 6) Info.plist（python plistlib，见 4.3）

# 7) 签名
ldid -S NativeAdrenoMetal.bundle/NativeAdrenoMetal
```

### 7.5 本地化时必须改的三处

| 位置 | 改法 | 原因 |
|---|---|---|
| `FrontendChecks.h` | 注释掉 `waitUntilSignaledValue:timeoutMS:` 检查 | iOS 16 SDK 无此 iOS 17 选择器 |
| 链接命令 | 显式加 `-lc++ -lc++abi` | 交叉 clang 不会自动加 C++ 运行库 |
| `-target` | 用 `ios16.0`（不是 17.0） | 设备是 iOS 16.7，min version 17 会拒绝加载 |

---

## 8. 刷机与验证步骤

```bash
# 1) 写回根镜像（16 GiB，约 5–6 分钟）
write_image.ps1 -Image iospull.img      # pinfile rawwrite

# 2) 刷启动镜像
adb push METAL-boot_a.img /data/local/tmp/m.img
adb shell su -c 'dd if=/data/local/tmp/m.img of=/dev/block/by-name/boot_a bs=1M conv=fsync'
adb reboot
```

### 验证清单（看内核持久日志）

| 检查 | 期望 |
|---|---|
| GPU 上电 | `PANDORA GPU: level 5 … level 14 done` |
| GMU 固件 | `GMU firmware loaded: … core fw version 50030019` |
| HFI | `HFI perf table ACKED` / `HFI START ACKED` |
| DCVS | `PANDORA GPU DCVS: on, 160..1200 MHz` |
| GPU 负载 | `DCVS: busy N%` 中的 N **> 0**（说明有客户端在提交） |
| 扫描输出 | `PANDORA SCANOUT` 出现、`SWEET FB: stats` 里不再有 `copy avg` |
| 触摸 | `PANDORA TOUCH: mux N -> 0`（成功） |
| 锁屏 | 顶部时间 + 底部上滑条 |

### 读日志

```bash
adb exec-out "su -c '/data/local/tmp/pinfile rawread /data/local/tmp/ioslog.img \
  $(readlink -f /dev/block/by-name/userdata)'" > ioslog.bin
python read_log_boot2.py
```

---

## 9. 回滚

| 场景 | 操作 |
|---|---|
| 内核出问题 | 9008 刷回原版 `boot_a.img` |
| 镜像出问题 | 恢复之前的 `iospull.img` 备份（`write_image.ps1`） |
| 只想关 GPU | 启动参数去掉 `pandora_gpu`，或 `pandora_gpu=0` |
| 扫描输出黑屏 | `pandora_scanout=0` 回到 CPU 拷屏 |

---

## 10. 文件清单

| 文件 | 说明 |
|---|---|
| `METAL-boot_a.img` | **当前最佳配置**（GPU + 插件 + scanout） |
| `TOUCH3-boot_a.img` | 触摸可用版（scanout=0） |
| `GPUFW-boot_a.img` | GPUFW 版（固件包，无插件） |
| `FINAL5-boot_a.img` | 首次进锁屏版 |
| `iospull.img` | 16 GiB 根镜像（含 MG 容器 + 插件） |
| `iosfw.img` + `iosfw_extents.json` | GPU 固件包与 extents |
| `gpu-fw/gen80200_*` | 从手机取出的 GPU 固件 |
| `ioslog_lockscreen.txt` | 首次进锁屏的日志（8032 行） |
| `TONIGHT.md` / `FINAL-PLAYBOOK.md` | 方案与手册 |

### WSL 侧脚本

| 脚本 | 作用 |
|---|---|
| `wsl_build_plugin_local.sh` | 本地构建 Metal 插件（本文件第 7 节） |
| `wsl_metal_payload.sh` | 含插件信任的 payload |
| `wsl_install_plugin_img.sh` | 把插件装进镜像 |
| `wsl_make_mgc.sh` | 创建 MobileGestalt 容器 |
| `wsl_dl_tarballs.sh` / `wsl_mtdl.sh` | 多线程下载（含分段） |
| `wsl_build_ld64.sh` | 编译 cctools-port 的 Apple ld64 |
| `mk_plugin_tc.py` | 计算 cdhash 并生成 raw 信任模块 |

---

## 11. 已知问题 / 下一步

### 11.1 Metal 插件被 AMFI 拒绝（当前唯一阻塞）

现象（`pandora_gpu_metal=1` 时）：

```
PANDORA GPU UC: AGXAccelerator (IOAcceleratorES) registered, plugin NativeAdrenoMetal
IOK backboardd: matching ... IOProviderClass=IOAcceleratorES -> 1 found
AMFI: '/System/Library/Extensions/NativeAdrenoMetal.bundle/NativeAdrenoMetal' has no CMS blob?
AMFI: '...': Unrecoverable CT signature issue, bailing out.
AMFI: code signature validation failed.
```

已排除的项：

| 项 | 结论 |
|---|---|
| 文件是否一致 | ✅ 主机与镜像内 SHA256 相同（`c7304c08…`，22,438,240 B） |
| 签名是否存在 | ✅ `LC_CODE_SIGNATURE` 存在，CD v0x40200、hashType=2(SHA-256)、pageSize=4096 |
| cdhash | `117aece1f54ea00c8fc706c100fb316289c16f1e` |
| 信任模块是否加载 | ✅ `loaded external trust cache module: 0/1` |
| 放错模块 | 已修正：插件 cdhash 并入 **module 0（static）**，共 2761 条 |
| GPU 服务发布 | ✅ `AGXAccelerator (IOAcceleratorES) registered`，backboardd `1 found` |

结论：**信任缓存条目没有命中**，AMFI 退回到 CoreTrust 校验，而 ad-hoc 签名没有 CMS 证书链 → 拒绝。

### 11.2 三个可选修法（按成本排序）

**A. 启动参数旁路（最快，只改 payload，不重编内核）**

内核里存在 `amfi_get_out_of_my_way` 这个 boot-arg（已用 `strings` 在 `k4-amfi.macho` 中确认）：

```
... 原命令行 ... pandora_gpu=14 pandora_gpu_metal=1 amfi_get_out_of_my_way=1
```

若无副作用，插件即可加载；代价是代码签名强制整体放宽（移植机可接受）。

**B. 用 macOS 的 `codesign` 重新签名（最正统）**

`metal.yml`（已提交）在 macOS runner 上用
`codesign -s - -f --identifier org.blu-sharky.NativeAdrenoMetal` 签名，与 pandora 项目一致。
等 CI 出包后替换本地 bundle，重新算 cdhash、重打 payload 即可。

**C. 内核补丁：让 AMFI 认可 static 信任缓存里的 bundle**

参考 `xnu/patches/0151-pandora-original-amfi-early-init.patch`，在 AMFI 的
`has no CMS blob` 分支之前加一次按 CDHash 的 `trust_cache` 查询，命中即放行。
需要重编内核（本地 3–5 分钟），是长期正解。

### 11.3 其它已知问题

| 项 | 状态 |
|---|---|
| 普通 App 启动（依赖 Metal 的） | 待插件加载成功 |
| `mediaserverd`、`CollectionsPoster` 被 SIGKILL(0x0) | GPU 接通后应减少 |
| 音频（DSP/SMP2P/GLINK） | 未做 |
| Wi-Fi / 蜂窝 / 相机 | 未做 |
| 120 Hz | 扫描输出跑通后再做时序与 iOS 判定 |

### 11.4 性能优化清单（条件已具备，等 GPU 接通后做）

| 优化 | 做法 | 预期 |
|---|---|---|
| 去掉 CPU 拷屏 | GPU 出帧后 `pandora_scanout=1` | 省 3.4 ms/帧 |
| 提高呈现频率 | `sweetfb_hz=60`（CPU 拷屏受 copy 3.4 ms 限制） | 30→60 Hz |
| GPU 调频 | DCVS 已开（160–1200 MHz，16 ms 周期） | 自动 |
| 触摸延迟 | `pandora_touch` 轮询 4/8 ms → 2/4 ms | 更跟手 |
| 预编译着色器 | pipeline cache 放进镜像 | 首次开 App 不卡 |


---

## 12. 关键教训（避免重走弯路）

1. **不要用 17 Pro 的参数**：触摸供电 `L4I_E0` 在 Z80 上不存在，必须 `L4B_E0`/`L14B_E0`。
2. **内核改动必须有可见证据**：把 cmd-db 转储提前到 0.2 秒，才在 2.7 秒挂死的情况下拿到资源名。
3. **信任缓存是进系统的前置**：静态信任缓存让 launchd 能 exec；插件也要单独加 cdhash。
4. **ld64.lld ≠ Apple ld64**：kalloc_type 合成只有 Apple ld64 有。
5. **MobileGestalt 容器缺失 = lsd 死锁 = 永久 logo**，且日志里只表现为 `___ulock_wait`。
6. **本地重链接必须显式列出所有改过的文件**：`local_build.sh` 只用 CI 对象补其余文件；
   漏传 `bsd/dev/pandora_touch.c` 就会退回 CI 里旧的 `L4I_E0`，触摸立刻失效。
7. **驱动参数要能从手机 DT 生成**：`tools/gen_z80_gpu_tables.py` 读 `/sys/firmware/fdt`，
   输出 `gpu_opps`/`gmu_opps`/`gpu_opp_bw`，别再手工抄别的机型。

---

## 13. 2026-10-28 实测记录（GPU 插件加载）

### 13.1 从 Z80 自己的 DT 生成的 GPU 表（已验证）

`tools/gen_z80_gpu_tables.py` 读手机 live DT（`/sys/firmware/fdt`：

```
qcom,kgsl-3d0@3d00000  qcom,gpu-model = "Adreno840"
                       compatible = "qcom,adreno-gpu-gen8-2-0"
reg            kgsl 0x3d00000/0x40000, rscc 0x3d50000/0x10000,
               cx_dbgc 0x3d61000/0x3000, cx_misc 0x3d9e000/0x2000
interrupts     SPI 0x12c (300) kgsl_3d0_irq, SPI 0x50 (80) cx_host_irq
qcom,chipid    0x44050a00
initial-pwrlevel = 14 (222 MHz)
qcom,gmu@3d37000  reg gmu 0x3d37000/0x68000 + gmu_ao_blk_dec0 0x3d40000/0x10000
                  iommus <0x377 5>, gmu-freq-table = 475 MHz (0x38), 700 MHz (0x80)
```

生成的表（17 档，**最高 1050 MHz**）：

```
{160,0x32}{191,0x33}{222,0x34}{282,0x36}{342,0x38}{382,0x3c}{422,0x40}{461,0x4c}
{500,0x50}{539,0x60}{578,0x80}{646,0x90}{726,0xc0}{826,0xe0}{902,0x100}{967,0x140}{1050,0x1a0}
GMU: {475,0x38}{700,0x80}
bus: 1,2,2,2,2,2,3,3,3,3,3,5,5,7,7,7,11
```

实测：`HFI perf table ACKED`、`level 14 (user interface) done` —— 表被 GMU 接受。
（17 Pro 的旧表是 18 档、含 1100/1200 MHz、GMU 4 档，已清除。）

### 13.2 GPU 服务与打开类型（实测）

| 配置 | `open AGXAccelerator type 1` | 插件是否运行 |
|---|---|---|
| type 1 接受（`PandoraGpuAccel`） | `-> 0x0`（成功） | ❌ 无 `0x4147` 打开 |
| type 1 拒绝（原生） | `-> 0xe00002c7`（Unsupported） | ❌ |

驱动日志里**始终没有** `PANDORA GPU UC: client N opened ...`，即插件从未打开设备。

### 13.3 插件 bundle 封装的实测（关键否定结果）

| 镜像 | 内核 | 结果 | 标记 |
|---|---|---|---|
| 无 `_CodeSignature` | type1 接受 | ✅ 进锁屏、触摸正常 | GPU `busy 0%` |
| 有 `_CodeSignature` | type1 接受 | ❌ 卡 logo | — |
| 有 `_CodeSignature` | type1 拒绝 + 标记日志 | ❌ 卡 logo | **`PANDORA GPU MARK` 一条都没有** |

结论：

1. `ldid -S <bundle>` 能生成 `_CodeSignature/CodeResources`（只封 `Info.plist`，SHA-1）
2. 但加封装**并不能**让 Metal 加载插件（无 `0x6800` 标记）
3. 加封装还会让系统卡 logo，原因不在插件（插件没跑）

### 13.4 下一步（按优先级）

1. **弄清 Metal 的插件发现路径**：UniXNU 用 `add-gpu-plugin-data.py --publish base.dbr gpu.dbr`
   把插件写进 **Data seed**，再用 `--data-ramdisk` 随 payload 下发；我们目前只是把文件
   放进根镜像的 `/System/Library/Extensions`，可能不是 iOS 16 实际查找的位置。
2. 在 `AGXAccelerator` 服务上补齐 Metal 需要的属性（除 `MetalPluginName`/`MetalPluginClassName`
   外，IOGPU 还会读 `IOGLBundleName`、`MetalPluginCanBeUsedAsAccelerator` 等）。
3. 观察 backboardd 是否真的调用 `IOServiceMatching("IOAcceleratorES")` 后去 `dlopen`
   —— 可用 `DYLD_PRINT_*` 或插件内 `mark()` 验证（当前连 `0x6800` 都没有，说明没被 dlopen）。

### 13.5 当前可用基线

`METAL10-boot_a.img`（type1 接受 + 无封装 + `L4B_E0` 触摸轨）：

- 进锁屏、触摸正常、设置不再黑屏
- GPU `busy 0%`、相册闪退、滑动卡顿

镜像：`iospull.img`（MG 容器 + backboardd 偏好 + 无封装插件）。

---

## 14. 2026-10-28 鏅氾細鍗?logo 鐨勭湡姝ｆ牴鍥?鈥斺€?MG 缂撳瓨缂哄け锛堝凡淇锛?
### 14.1 鐥囩姸涓庤鍒?
鍚屼竴濂楀唴鏍?闀滃儚锛?*鏈夋椂鑳借繘閿佸睆銆佹湁鏃跺崱 logo**锛岀湅璧锋潵鍍?鐜勫"銆?鏈熼棿璇垽杩囷細GPU 鎻掍欢 init 鎸備綇銆乥undle 灏佽鏈夐棶棰樸€乼ype 1 澶勭悊鏈夐棶棰樸€?
### 14.2 鐪熸鍘熷洜

闀滃儚閲屾湁 MG 瀹瑰櫒鐩綍锛屼絾**娌℃湁缂撳瓨鏂囦欢**锛?
```
/private/var/containers/Shared/SystemGroup/
    systemgroup.com.apple.mobilegestaltcache/
        .com.apple.mobile_container_manager.metadata.plist   鉁?鍦?        Library/Caches/                                       鉁?鍦?        Library/Caches/com.apple.MobileGestalt.plist          鉂?缂?```

缂哄皯缂撳瓨鏃?libMobileGestalt 璧?棣栨鍒涘缓"璺緞锛屽湪 containermanagerd 灏氭湭灏辩华鏃?**姝荤瓑淇″彿閲?* 鈫?lsd 鐨勭嚎绋嬪叏閮ㄥ牭浣?鈫?LaunchServices 寤哄簱姘歌繙涓嶅畬鎴?鈫?鍗?logo銆?绗﹀彿鍖栨爤鍥哄畾涓猴細

```
_semaphore_wait_trap
_dispatch_sema4_wait
_dispatch_semaphore_wait_slow
func_19c57c4bc  func_19c57c6e8  func_19c57c0f8  func_19c571770
```

### 14.3 淇锛堜竴琛屾枃浠讹級

鍐欏叆 `/private/var/containers/Shared/SystemGroup/
systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist`锛?binary plist锛?
```python
import plistlib
inner = plistlib.dumps({}, fmt=plistlib.FMT_BINARY)      # CacheData锛歡estalt 瀛楀吀
outer = {'CacheVersion': 1, 'CacheData': inner}
open('com.apple.MobileGestalt.plist','wb').write(
    plistlib.dumps(outer, fmt=plistlib.FMT_BINARY))
```

灞炰富 `0:0`銆佹潈闄?`644`銆?
**鏁堟灉**锛氬啓鍏ュ悗鍚屼竴娆″紑鏈?19 鍒嗛挓鍐呭畬鎴愬缓搴撳苟**杩涢攣灞?*锛堟鍓嶈繛缁け璐ワ級銆?
### 14.4 鐢辨寰楀埌鐨勩€屽紩瀵煎悗瀹屾暣銆嶉暅鍍忥紙鏂板熀鍑嗭級

杩涢攣灞忓悗鎶?16 GiB 闀滃儚浠庢墜鏈烘媺鍥烇紙`pull_iosroot.ps1`锛?61 绉掞級锛?
| 妫€鏌ラ」 | 淇鍓?| **GOLDEN 闀滃儚** |
|---|---|---|
| LaunchServices `csstore` | 0 | **2** 鉁?|
| MG 缂撳瓨 plist | 0 | **1** 鉁?|
| SystemGroup UUID 瀹瑰櫒 | 68 | **100** 鉁?|
| 杩佺Щ鏍囪 | 鏈?| 鏈?鉁?|
| disabled.plist / bb 鍋忓ソ / 鎻掍欢 | 鏈?| 鏈?鉁?|

浜х墿锛歚iosroot-GOLDEN.img`锛?7,179,869,184 B锛宖sck -fy 鍚?OK锛夈€?
### 14.5 宸ヤ綔娴佺▼鏁欒锛堥噸瑕侊級

1. **涓嶈鍐嶆嬁鏈湴鏃у壇鏈鐩栨暟鎹垎鍖?*锛歩OS 姣忔鍚姩閮戒細鍐欏叆鐘舵€侊紙搴撱€佺紦瀛樸€佸鍣級锛?   瑕嗙洊浼氭妸瀹冩姽鎺夛紝浜庢槸姣忔閮借閲嶆柊寤哄簱銆侀噸鏂拌俯 MG 绔炰簤銆?2. **瀹為獙鍙崲 payload / 鍐呮牳**锛涘繀椤绘敼闀滃儚鏃讹紝鍏?`pull_iosroot.ps1` 鎷夊洖鏈€鏂帮紝鍐嶆敼銆?3. 鍒ゆ柇銆屽崱浣忋€嶈繕鏄€屽湪寤哄簱銆嶏細鑷冲皯绛?25鈥?0 鍒嗛挓锛涘缓搴撴湡闂?CPU 楂樸€佹棩蹇楁寔缁闀裤€?
---

## 15. 2026-10-29 鍑屾櫒锛欸PU 鎻掍欢缁堜簬璺戣捣鏉ヤ簡锛堝崱鍦?Vulkan loader 璺緞锛?
### 15.1 涓夊潡缂哄け鐨勬嫾鍥撅紙閮藉凡瀹氫綅锛?
| 缂哄け | 鐜拌薄 | 淇 |
|---|---|---|
| `pandora_gpu=14` | 鐢ㄦ埛瀹㈡埛绔笉瀹屾暣 | **`pandora_gpu=15`**锛堝畼鏂?`gpu_test.sh: LEVEL=15`锛?|
| `NativeAdrenoMetal.clients` | 鎻掍欢涓嶇粰浠讳綍杩涚▼鍙戣澶?| 鍐欏叆 `/System/Library/Extensions/NativeAdrenoMetal.clients`锛屽唴瀹?`backboardd` |
| Turnip 鏈畨瑁?| 鎻掍欢鏍囪瑙ｇ爜涓?`no-turnip ` | `libvulkan_freedreno.dylib` 鈫?`/usr/lib/` |

### 15.2 鏍囪鏃ュ織鐨勭敤娉曪紙鍏抽敭璋冭瘯鎵嬫锛?
鍐呮牳鎶婃彃浠剁殑 `gpu.mark()` 鎵撳嵃鎴?`PANDORA GPU MARK: 4147XXXX by <杩涚▼>`锛?鍏朵腑浣?8 浣嶆槸 ASCII 瀛楃銆乣0x7fff` 鏄垎闅旂銆傝В鐮佽剼鏈紙鏈粨搴撳伐鍏凤級锛?
```python
marks = [(int(m.group(1),16)) for ... ]
b = v & 0xff
```

瀹炴祴搴忓垪锛?
| 閰嶇疆 | 瑙ｇ爜缁撴灉 |
|---|---|
| 鏃?Turnip | `no-turnip ` |
| 瑁呬簡 Turnip锛坄libvulkan_freedreno.dylib`锛?| `no-device device: LoaderNotFound(LoaderNotFound { tried:` |

璇存槑鎻掍欢**宸茶鍔犺浇骞惰繍琛?*锛孴urnip 妫€鏌ュ凡閫氳繃銆?
### 15.3 鍓╀綑闃诲锛歏ulkan loader 鍚嶅瓧

`metal2vulkan-runtime/src/device/create.rs: default_loaders()` 鍦?iOS锛堥潪 macos/android/linux锛?钀藉埌 `else` 鍒嗘敮锛屽彧灏濊瘯锛?
```rust
vec![PathBuf::from("libvulkan.so.1")]
```

鎴戜滑鏀惧叆 `/usr/lib/libvulkan.so.1`锛圱urnip 鐨勫壇鏈級浠嶇劧 `LoaderNotFound`鈥斺€?寰堝彲鑳芥槸 backboardd 娌欑涓嶅厑璁?dlopen 涓嶅湪 dyld 鍏变韩缂撳瓨涓殑 `/usr/lib` 搴撱€?
### 15.4 涓嬩竴姝ワ紙涓ゆ潯锛屽缓璁厛鍋?A锛?
**A. 璁?loader 鎸囧悜鎻掍欢鑷繁鐩綍閲岀殑 Turnip锛堟渶鍙兘鎴愬姛锛?*

1. 鎶?Turnip 鏀捐繘 `/System/Library/Extensions/NativeAdrenoMetal.bundle/`
2. 缁?metal2vulkan-runtime 鎵撹ˉ涓侊細`default_loaders()` 鐨?iOS 鍒嗘敮鍏堝皾璇?   `/System/Library/Extensions/NativeAdrenoMetal.bundle/libvulkan_freedreno.dylib`
3. 閲嶇紪 Rust 妗?+ 鎻掍欢锛坄wsl/build_metal_plugin_linux.sh`锛岀害 5 鍒嗛挓锛?4. 閲嶆柊绠?cdhash 鈫?淇′换妯″潡 鈫?閲嶆墦 payload 鈫?瑁呴暅鍍?
鎻掍欢鐩綍鏄畠宸茬粡鑳借鐨勪綅缃紙`.clients` 灏辨斁鍦ㄩ偅閲岋級锛屾矙绠卞繀鐒舵斁琛屻€?
**B. 璁?backboardd 鐨勬矙绠辨斁琛?`/usr/lib/libvulkan.so.1`**
锛堟敼 sandbox profile锛屾垨鎶婂簱鏀捐繘 dyld 鍏变韩缂撳瓨鈥斺€旀垚鏈洿楂橈級

### 15.5 浠婃棩宸茬‘璁ょ殑鍙敤鍩虹嚎

- 闀滃儚锛歚iosroot-SEAL.img`锛圙OLDEN + 灏佽鎻掍欢 + clients + Turnip + loader 鍒悕锛屽亣 MG 缂撳瓨宸插垹锛?- 鍐呮牳锛歚METAL14-boot_a.img`锛坄pandora_gpu=15`銆乑80 OPP 17 妗ｃ€乣L4B_E0`銆佹爣璁版棩蹇楋級
- 鍚姩锛?*绾?2鈥? 鍒嗛挓杩涢攣灞?*锛堝簱宸插缓濂斤級锛岃Е鎽告甯?- GPU锛氭彃浠跺凡鍔犺浇锛屽崱鍦?Vulkan loader

---

## 16. 2026-10-29 00:30 鈥斺€?GPU 鐪熸璺戣捣鏉ヤ簡锛堟柊閲岀▼纰戯級

### 16.1 瑙ｉ攣 GPU 鐨勬渶鍚庝竴鎶婇挜鍖欙細Turnip 涔熻杩涗俊浠荤紦瀛?
涔嬪墠鐨勫け璐ユ爣璁?`no-device: LoaderNotFound` 涓嶆槸璺緞闂锛堟彃浠堕噷 `default_loaders()`
宸叉寜鎴戜滑琛ヤ竵鍏堟壘 bundle 鍐呰矾寰勶級锛岃€屾槸 **AMFI 鎷掔粷 dlopen 鏈彈淇＄殑 Turnip**銆?
鎶?Turnip 鐨?cdhash 涔熷苟鍏?static 淇′换妯″潡鍚庯紝鏃ュ織绔嬪埢鍙樻垚锛?
```
PANDORA GPU UC: client 0 opened by backboardd through AGXAccelerator   鈫?鎻掍欢杩炰笂 GPU
PANDORA GPU IRQ: 1062 interrupts (1059 fences)                        鈫?涓柇姝ｅ父锛屾覆浜?1059 甯?PANDORA GPU DCVS: busy 99%, MHz:time 1050:100%, now 1050 MHz           鈫?婊¤浇鏈€楂橀
```

**杩欐槸鏈」鐩涓€娆¤ Adreno 840 鍦?iOS 涓婄湡姝ｆ墽琛?Turnip 鎻愪氦銆?*

### 16.2 鏂扮殑銆佷篃鏄渶鍚庣殑闃诲锛欱uffer Object 鍒嗛厤鑰楀敖

```
PANDORA GPU UC: client 0 method 2 in 16 out 8 -> 0xe00002bd    鈫?kIOReturnNoMemory
PANDORA GPU UC: client 0 short fence polls timed out 59001 times
PANDORA GPU HANG 脳16                                           鈫?绾?120 绉掑悗 GPU 鎸備綇
```

`method 2` = `NativeAdrenoMethodBoAlloc`锛坄PandoraAdrenoGpu.cpp: AGXDeviceUserClient::method`锛夛紝
瀹冭皟鐢?`pandora_gpu_bo_alloc(client_, bytes, align_log2, &handle)`銆?
鍓?1059 甯т竴鍒囨甯革紝涔嬪悗鍒嗛厤寮€濮嬪け璐?鈫?鍏稿瀷鐨?**BO 娉勬紡 / 姹犺€楀敖**
锛堟垨 `NativeAdrenoAbiMaxBoBytesShift` 涓婇檺杩囦綆锛夈€?
### 16.3 涓嬩竴姝ワ紙鏄庣‘锛?
1. 鍦?`pandora_gpu_bo_alloc` 閲屽姞璁℃暟涓庝笂闄愭棩蹇楋紝纭鏄€屾€婚噺瓒呮睜銆嶈繕鏄€屽崟娆¤繃澶с€?2. 妫€鏌?`NativeAdrenoMethodBoFree` 鏄惁鐪熺殑鎶?buffer 杩樼粰姹狅紙瀵圭収 client 鐨?free 璺緞锛?3. 瀵圭収涓婃父 `NativeAdrenoClient.cpp` 鐨?BO 鐢熷懡鍛ㄦ湡锛堝紩鐢ㄨ鏁般€乧lient teardown锛?4. 蹇呰鏃舵彁楂樻睜澶у皬锛堝綋鍓?1 GiB锛岃 ADT `--gpu-pool`锛?
### 16.4 褰撳墠鍙敤閰嶇疆锛堟渶寮虹増锛?
| 缁勪欢 | 鍊?|
|---|---|
| 鍐呮牳 | `METAL16-boot_a.img`锛坄pandora_gpu=15`銆佹爣璁版棩蹇椼€乑80 OPP 17 妗ｃ€乣L4B_E0`锛?|
| 闀滃儚 | `iosroot-SEAL.img`锛圙OLDEN + 搴撳凡寤?+ MG 缂撳瓨鐢?iOS 鐢熸垚 + 灏佽鎻掍欢 + bundle 鍐?Turnip + `.clients=backboardd`锛?|
| 淇′换 | static 妯″潡鍚?**鎻掍欢 cdhash `cbce9ba2鈥** 涓?**Turnip cdhash `899d39cd鈥** |
| 鍚姩 | 2鈥? 鍒嗛挓杩涢攣灞忥紝瑙︽懜姝ｅ父 |
| GPU | 鉁?宸叉墽琛?1059 甯с€佷腑鏂甯革紱鉂?BO 鍒嗛厤鑰楀敖鍚庢寕璧?|

### 16.5 鐥囩姸瀵圭収

| 鐜拌薄 | 鍘熷洜 |
|---|---|
| 澹佺焊绾壊 | AegirPoster 娓叉煋澶辫触锛圙PU 鎻愪氦鍚庢嬁涓嶅埌瀹屾垚/鍐呭瓨锛?|
| 鐩稿唽闂€€ | 鍚屼笂锛屽垎閰嶅け璐?|
| 璁剧疆 About 鍗℃ | MobileGestalt 鐩稿叧锛堝彟涓€涓嫭绔嬮棶棰橈級 |
| 婊戝姩鎱?| 鍚堟垚甯ф渶缁堜粛璧?CPU 鎷峰睆锛屼笖 GPU 鎸傝捣鍚庢棤鍔犻€?|

---

## 17. 2026-10-29 13:00鈥?4:25 鈥斺€?GPU 椹卞姩淇ソ鐨勬渶鍚庝袱涓牴鍥狅紙vktest 瀹炴祴锛?
### 17.1 姝ラ 3.4 缁堜簬璺戦€氾細鐢ㄥ畼鏂?`sweetexecswap` 璺?vktest

launchd 鏂瑰紡瑁?`vktest` 鏃犳晥锛堟枃浠朵笉钀界洏銆乨aemon 涓嶆墽琛岋級銆傛纭仛娉曟槸椤圭洰鑷甫鏈哄埗锛?
```
sweetexecswap=bluetoothd:/usr/bin/vktest
```

鍗崇敤鍐呮牳鐨?exec-swap 鎶婃煇涓紑鏈哄繀鍚殑瀹堟姢杩涚▼鏇挎崲鎴?vktest锛岃緭鍑虹洿鎺ヨ繘鍐呮牳鏃ュ織銆?娉ㄦ剰锛?*琚浛鎹㈢殑杩涚▼蹇呴』鍦?`disabled.plist` 閲屾槸鍚敤鐨?*锛堟垜浠師鏉ョ鐢ㄤ簡 bluetoothd锛?鎵€浠ユ浛鎹笉瑙﹀彂锛涙妸 `com.apple.bluetoothd` 鏀规垚 `false` 鍚庢墠璺戣捣鏉ワ級銆?
### 17.2 vktest 绗竴杞粨鏋滐細GPU 娓叉煋姝ｇ‘锛屼絾 550 绉掑悗姘镐箙鍋滀綇

```
VKTEST: part 1: vkCreateInstance -> 0
VKTEST: device 'Adreno (TM) 840 (raytracing disabled)' vendor 0x5143 device 0x44050a31 api 1.4.354
VKTEST: part 3: pixels ffbf8040 ... 0 of 4096 wrong.      鈫?鍍忕礌鍥炶姝ｇ‘
VKTEST: part 5: pixel (8,8) ff00ff00 (56,56) ff000000; 0 clearly wrong   鈫?涓夎褰㈡纭?VKTEST: PASS (0 failures)   鈫?鍓?~550 绉掑弽澶嶉€氳繃
...
VKTEST: part 2/3/4/5: vkWaitForFences -> 2 FAIL (VK_TIMEOUT)
VKTEST: FAIL (4 failures)   鈫?姝ゅ悗姘镐箙澶辫触
```

### 17.3 鏍瑰洜锛歳ing 鍐欐寚閽堣鐩栨湭娑堣垂鍛戒护

`gpu_submit()`锛坄bsd/dev/pandora_gpu.c`锛夋瘡娆℃彁浜ゅ線 ring 鍐欑害 40 涓?dword锛?**浣嗕粠涓嶆鏌?`CP_RB_RPTR`**銆俽ing 鍙湁 32 KiB锛?192 dword锛岀害 200 娆℃彁浜や竴鍦堬級锛?涓€鏃?GPU 钀藉悗锛屽啓鎸囬拡杩戒笂鏈秷璐瑰懡浠?鈫?CP 鍋滄 鈫?涔嬪悗鎵€鏈?fence 姘镐笉瀹屾垚
锛堣〃鐜帮細鐢婚潰鍗′綇銆丟PU 浠?1050 MHz 婊¤浇鍙戠儹锛夈€?
**淇**锛堥〉闈€宺ing 鎺ㄨ繘 / fence 鍗′綇鏃朵粛浼氬け鏁堛€嶏級锛?
```c
static void
ring_wait_space(uint32_t need)
{
	const uint32_t total = (uint32_t)(gpu_bufs[0].bytes / 4);
	for (unsigned spin = 0; spin < 2000; spin++) {
		const uint32_t rptr = gr(CP_RB_RPTR) % total;
		const uint32_t used = (ring_wptr + total - rptr) % total;
		if (used + need + 8 < total) {
			return;
		}
		delay(1);
	}
	printf("PANDORA GPU: ring full (wptr %u rptr %u need %u)\n", ring_wptr, gr(CP_RB_RPTR), need);
}
```
鍦?`gpu_submit()` 寮€澶磋皟鐢?`ring_wait_space(96);`銆?
### 17.4 淇鍚庡疄娴嬶紙鍐冲畾鎬э級

```
VKTEST: PASS (0 failures)  脳35   鏈€鍚庝竴娆?3464.889 s锛堢害 58 鍒嗛挓锛?VKTEST: FAIL               脳0
[bo pool] used 1291/4096 580 MiB, free 548 MiB, extents 11/1024   锛堢ǔ瀹氾級
```

**GPU 鍦ㄦ弧杞戒笅绋冲畾杩愯 58 鍒嗛挓銆佸儚绱犲畬鍏ㄦ纭€侀浂澶辫触銆?*
姝ゅ墠 `2026-10-29 00:30` 鐨勫彟涓€澶勪慨澶嶏紙`GPU_POOL_EXTENTS 512鈫?024`锛夎В鍐充簡
`kIOReturnNoMemory` 涓?`PANDORA GPU HANG`锛涗袱澶勫悎璧锋潵鎵嶈 GPU 鐪熸绋冲畾銆?
### 17.5 鐜板湪鍓╀笅鐨勯棶棰樹笉鍦?GPU 娓叉煋

GPU 娓叉煋宸查獙璇佹纭笖绋冲畾锛屽墿涓嬬殑鐢ㄦ埛鍙闂灞炰簬**鍚堟垚/鍛堢幇璺緞**锛?
| 鐜拌薄 | 褰掑睘 |
|---|---|
| 閿佸睆/妗岄潰澹佺焊绾壊 | CARenderServer 鍚堟垚鏈蛋 GPU锛坧ublish 妯″紡 / CA 璺緞锛?|
| 蹇€熸粦鍔ㄦ椂鐢婚潰寮傚父 | 鍛堢幇涓庡抚鍚屾锛圕PU 鎷峰睆 vs GPU 鎻愪氦锛?|
| 璁剧疆 About 鍗℃ | MobileGestalt 鏌ヨ锛堜笌鏈棶棰樻棤鍏筹級 |
| 鐩稿唽闂€€ | 涓婂眰渚濊禆锛圓ssetsLibrary/濯掍綋锛夛紝闇€鍦ㄥ叾涓婄户缁獙璇?|

### 17.6 鍏抽敭缁忛獙

1. **vktest 蹇呴』鍏堣窇閫?*锛氬畠鎶娿€孏PU 绠楅敊銆嶅拰銆屾樉绀洪摼璺敊銆嶅交搴曞垎寮€銆?2. **exec-swap 鐨勫涓昏繘绋嬪繀椤诲惎鐢?*锛氱鐢ㄥ垪琛ㄩ噷鐨勮繘绋嬩笉浼氳鏇挎崲銆?3. **ring 绌洪棿妫€鏌ヤ笉鑳界渷**锛氱煭鏃堕棿姝ｅ父銆侀暱鏃堕棿蹇呮寕锛屽鏄撹璇垽涓恒€孏PU 鍋跺彂鏁呴殰銆嶃€?4. **`[bo pool]` 琛屽緢鏈夌敤**锛氳兘绔嬪埢鎺掗櫎銆屽唴瀛樻硠婕?姹犺€楀敖銆嶈繖绫昏鍒ゃ€?
