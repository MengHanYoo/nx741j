# T2：编译与厂商内核兼容的模块，读运行中的 MCQ/队列地址

目标：拿到厂商内核真正在用的 MCQ 队列基址、门铃地址与队列配置，用来决定 `NativeUfs` 是复刻 MCQ
还是先关 MCQ 走传统门铃。

## 现状（2026-11-11 20:4x）

| 项 | 状态 |
|---|---|
| 手机 | **不在线**：`adb devices` 为空、无 9008 设备（20:41 检查） |
| 厂商内核版本 | `6.12.38-android16-5-zte-1006-4k`（clang 19.0.1，kleaf 构建，SMP PREEMPT） |
| 6.12.38 源码 | 本地没有；`/opt/ghcache` 也没有 ACK 6.12 |
| 本地 vendor_dlkm 转储 | `_tmp/vendor_dlkm_a.img` 里的模块 vermagic 是 **6.12.23**，与本机 6.12.38 不匹配 → 需从设备重新拉 |
| 已确认的运行态 | `use_mcq_mode=Y`；`/proc/iomem` 给出 `mcq 0x1da0000`、`mcq_vs 0x1da4000`、`mcq_sqd 0x1da5000`；8 条 `qcom-mcq-esi` 在计数 |
| XPU | DTB 无 `xpu` 节点、vmlinux 无 `msm_xpu` 符号 → 本机 XPU **不由 Linux 驱动管理**（固件配置） |

## 已准备的产物（`_tmp/t2/`）

| 文件 | 用途 |
|---|---|
| `mcq_dump.c` | 只读模块：ioremap `ufs_mem`/`mcq`/`mcq_sqd`/`MCQCFG(0x300)`，把寄存器打进 dmesg。**不写任何寄存器** |
| `Makefile` | 标准外挂模块构建 |
| `build.sh` | 用 ACK 6.12 源码 + `Module.symvers`（由 kallsyms 的 `__crc_*` 生成）编译 |
| `ondevice.sh` | 设备侧一次性采集：版本/config/CRCs/`resource*`/debugfs/`show_hba`/dmesg |

## 设备回来后的顺序

1. `adb shell sh ondevice.sh > t2-ondevice.txt`：先看能不能**不走模块**就拿到数据——
   `mount -t debugfs` 后 `cat /sys/kernel/debug/ufshcd0/show_hba` 如果存在，直接就是活寄存器；
   `/sys/devices/platform/soc/1d84000.ufshc/resource*` 若可读，也能 mmap/`dd` 读到 MCQ 窗口。
2. 取 `vermagic`（`modinfo` 或任意 .ko 的 `.modinfo`）与 `/proc/config.gz` 的 `CONFIG_MODVERSIONS`、
   `CONFIG_MODULE_SIG*`，决定是否需要签名。
3. 从 `/proc/kallsyms` 的 `__crc_*` 生成 `Module.symvers`；拉设备上真实的 vendor modules
   （`/vendor/lib/modules` 或 `vendor_dlkm` 分区）作 CRC 交叉校验。
4. 联网一次把 ACK 6.12 缓存进 `/opt/ghcache`，之后离线编译：
   `CONFIG_LOCALVERSION=-android16-5-zte-1006-4k` 让 vermagic 对齐。
5. `insmod mcq_dump.ko`，`dmesg | grep mcq_dump` 取寄存器表。

## 要读的寄存器（读出来后按这张表解码）

| 地址 | 含义 |
|---|---|
| `0x1d84300` | `MCQCFG`，bit0 = MCQ Enable（厂商 `ufshcd_mcq_disable` 就是清这一位） |
| `0x1d84000+0x30/0x34` | HCS / HCE |
| `0x1d84050/0x54` | UTRLBA / UTRLBAU（传统队列，MCQ 模式下可能为 0） |
| `0x1d84060/0x70/0x74/0x80` | UTRLRSR / UTMRLBA / UTMRLBAU / UTMRLRSR |
| `0x1da0000+0x00/0x04/0x08/0x0c/0x10/0x20`（每队列 0x40 步进） | MCQ 每队列寄存器：基址、属性、CID 等 |
| `0x1da0000+0x384/0x388` | MCQ ESI base / control |
| `0x1da5000` 起 | SQ doorbell 数组 |

## 风险

- **模块签名**：若 `CONFIG_MODULE_SIG_FORCE=y`，未签名模块无法加载；届时要么用 KSU 关校验，要么放弃模块走步骤 1 的 debugfs/resource 路线。
- **modversions**：CRCs 必须与内核完全一致；所以用本机 `/proc/kallsyms` 的 `__crc_*`，不用 6.12.23 的 .ko。
- 只读约束：`mcq_dump` 不写寄存器，读 MCQ/门铃窗口不会被 hardware 认为命令。
