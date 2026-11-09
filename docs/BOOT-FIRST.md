# 先让 iOS 起来（最小路径）

按官方[驱动顺序](https://unixnu.com/zh/porting/drivers)第 2 节的要求，这里只追一个判据：**设备上能把 XNU 拉起来、进到 iOS 用户态**。
第 3、4 级（显示驱动、触摸）以及 GPU/WiFi/音频等，等 iOS 起来之后再补——它们不影响内核启动。

## 现在卡在哪：不是驱动

| 层 | 状态 | 说明 |
|---|---|---|
| 1 efisp loader | ✅ | P6/P7 已实机验证 |
| 2 UEFI | ✅ | 4 MiB FD，DXE/BDS 正常 |
| 3 UniXNU 菜单 | ✅ | 菜单显示 NX741J，payload 卷挂载成功 |
| **4 菜单→DarwinBoot** | ❌ | 黑屏 → ZTE MemoryDump Mode；DARWINBOOT: Boot called 没出现 |
| 5 XNU | ⬜ | 从未启动 |

驱动序列表里第 1、2 级是“内核起来之后才会用到”的能力（平台服务、根卷）；当前层 4 还没把控制权交给 DarwinBoot，
所以**存储/显示驱动再完整也不会让 iOS 起来**。排查入口见 docs/LAYER4-FIXED.md、docs/LAYER4-PROBE.md。

## 本轮产物（可直接刷的镜像）

| 产物 | 大小 | sha256 |
|---|---|---|
| boot.img（V86 探针版） | 68,677,632 B | 5828e5a2a053a521b2fc0e3d314f73095a12af0552cf5a0f72013676c43b732c |
| payload.img（V86 探针版） | 67,108,864 B | a68ebc4e9b00bf43d4b6f19c1b197e46df6c4743b6b7ae64dee1fd1ec7a22e10 |
| kernel.macho（payload 内） | — | 36a618dbcb0b5e1736f0ea6596460e6c92a4b6d833818f7ac4bc5c4746793477 |
| boot 里的 kernel（入口补丁后） | — | 9964e55ae6e39c5fc9f8ecd036eb7f9a1e144fff4ba41cedcee899db93da16ee |
| ADT | — | be90306b72255c8e6934636db7dc261683350d9263f045a2937a301ddbf7bdf5 |
| 硬件 DTB | — | a5c109c0ca6fb30ebe34b915ea090ea4494fe000aadda92042c8575d5b2a8292 |
| FD（V86 探针版） | 4 MiB | dea20e0d8d343e1b46aa160911e0fda94fc5f9b650bd8dcf7ceacb6a633e73e2 |

构建命令（ext4 构建树，16 线程）：

```sh
cd ~/nx741j-build/UniXNU
./unixnu kernel  -D /mnt/e/.../repos/UniXNU-nx741j -j 16   # 全量 158 s
./unixnu payload -D /mnt/e/.../repos/UniXNU-nx741j -j 16
./unixnu bootimg -D /mnt/e/.../repos/UniXNU-nx741j
```

## 烧写与启动

9008 写入脚本：z80u-collect/wsl/flash-boota-9008.ps1（FD 走 LUN0 LBA 4760360、efisp 走 LUN4 LBA 748894；首次会话需要 Sahara）。
分区修复只能走 9008——fastboot 是受限 LinuxLoader。

## 启动参数

当前 kernel_args（unixnu.local.toml）：

```toml
kernel_args = ["-v", "serial=3"]
```

开存储路径时（**等层 5 起来再加**，现在加了不会执行，只会让日志更难读）：

```toml
kernel_args = ["-v", "serial=3", "native-source-storage=1", "native-ufs-root=1"]
```

本次新增的 SMMU 开关（smmu-probe=1、smmu-attach=1）**不要**在第一次启动时打开：它们会碰真实硬件寄存器，
等内核能起来、能抓日志之后再逐个试。

## 判定标准（按启动验证顺序）

| 看到什么 | 说明 |
|---|---|
| 菜单出现、NX741J 条目可选 | 层 3 正常 |
| DARWINBOOT: Boot called / ADT、Mach-O 相关打印 | 层 4 通了，问题转入层 5 |
| panic 文本或 green bar | 层 5 已进入内核，按 guide/debugging 抓 |
| 黑屏 + ZTE MemoryDump Mode | 仍在层 4（当前状态） |

## 第 1–4 级在“能启动”口径下的取舍

| 级 | 对启动的必要性 | 现在的状态 |
|---|---|---|
| 1 平台/内存 | **必需** | ✅ 编入并由 scripts/check-bringup.py 断言（GIC/定时器/8 CPU/memory/SMMU 节点） |
| 2 存储/根卷 | **必需**（层 5 之后） | 🟡 构建/链接就绪；运行时缺 xnu-8796 的 source-storage 补丁与策略信封投递 |
| 3 显示/亮度 | 非必需（GOP 已能出图） | 🟡 控制器 DT 就绪，内核驱动未写 |
| 4 输入 | 非必需 | ⬜ 未实现 |

把层 4 打通是当前唯一能让 iOS 起来的动作；驱动工作按上表排优先级即可。

## 层 5 诊断（层 4 通过之后）

1. **判定进入内核**：屏幕上出现 `DARWINBOOT: p15 entering the kernel at …` 之后才出现的画面/花屏/黑屏，
   都说明控制权已经交给 XNU；此时 `DARWINBOOT` 的行不会再增加。
2. **抓 panic**：payload 构建时已给内核打了 `--panic-screen --no-reset`（`patch-xnu-entry-probe.py`），
   内核 panic 会直接把文本画在 framebuffer 上、不自动重启；拍屏或用 UEFI 串口抓。
3. **内核参数**：当前 `keepsyms=1 debug=0x144 -v serial=3`。`-v` 会让 XNU 早期把日志画到屏幕上；
   如果屏幕全黑且无 panic，说明卡在 console 初始化之前（PE 早期），这时看 green bar 是否出现在最顶部。
4. **日志导出**：固件阶段的 `AdvancedLogDxe` 已在 FV 里（Depex 等待协议，不影响现有通道）；
   需要更细的固件日志时按 `docs/LAYER4-FIXED.md` 的导出步骤抓。

## 层 5 起来后再打开存储

到那时再改 `unixnu.local.toml`：

```toml
kernel_args = ["-v", "serial=3", "native-source-storage=1", "native-ufs-root=1"]
```

然后重跑 `payload` + `bootimg`（boot args 进的是 payload，不是 FD）。在此之前加了也不会执行——
source-storage 运行时补丁还没有 xnu-8796 版本，存储路径不会真的挂载根卷。
