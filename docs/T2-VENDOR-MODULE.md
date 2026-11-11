# T2：编译与厂商内核兼容的模块，读运行中的 MCQ/队列地址

目标：把厂商内核**工作中**的 MCQ 队列/门铃/ESI 与 apps-SMMU 配置导出来，
给 `NativeUfs` 一次可复制的已知good状态。

## 为什么是这条路（`docs/TASKS.md` 2026-11-11 晚的记录）

| 实验 | 读数 | 结论 |
|---|---|---|
| 传统门铃 0x58（自分配缓冲） | 写下去总线锁死 | 不可用 |
| 传统门铃（固件列表页 0xc3afc000） | 同样锁死 | 不是缓冲位置 |
| MCQ（按 `UFS-MCQ-RECIPE.md`，队列在自分配内存） | 同样锁死 | MCQ 门铃也挂 |
| MCQ（队列放固件页 0xc3afc000） | `far=sq+3, esr=0x96000047`（写翻译错误） | 内核写不进那段 DRAM，实验做不了 |
| 链路 / 时钟 | `hcs 0x10F`、`PA_PWRMode=FAST`、`gdsc=fa22f800`、`ahb=08000001` | 链路与时钟都正常 |

两条门铃都死锁 → 需要看**正在工作**的那份配置，于是有 T2。

## 现状（2026-11-11 20:4x）

| 项 | 状态 |
|---|---|
| 手机 | **不在线**（`adb devices` 空、无 9008 设备） |
| 厂商内核 | `6.12.38-android16-5-zte-1006-4k`（clang 19.0.1，kleaf，SMP PREEMPT） |
| 6.12.38 源码 | 本地与 `/opt/ghcache` 都没有 ACK 6.12 |
| 本地 vendor_dlkm 转储 | `_tmp/vendor_dlkm_a.img` 里模块 vermagic 是 **6.12.23**，与 6.12.38 不匹配 → 要重新从设备拉 |
| 已确认运行态 | `use_mcq_mode=Y`；`mcq@0x1da0000`、`mcq_vs@0x1da4000`、`mcq_sqd@0x1da5000`；8 条 `qcom-mcq-esi` 在计数 |
| XPU | DTB 无 `xpu` 节点、vmlinux 无 `msm_xpu` → 本机 XPU 不由 Linux 管，固件配置 |

## 产物（`_tmp/t2/`）

| 文件 | 用途 |
|---|---|
| `mcq_dump.c` | 只读模块：导出 `ufs_mem`(0x1d84000, 0x90)、`MCQCFG`(0x1d84300)、`mcq`(0x1da0000, N×0x40)、`mcq_vs`(0x1da4000)、`mcq_sqd`(0x1da5000)、`mcq_esi`(0x1da0380)、**apps-SMMU**（0x15000000：gr0 0x00–0xFF、SMR[0..7]、S2CR[0..7]） |
| `Makefile` / `build.sh` | 用 ACK 6.12 源码 + 由 `/proc/kallsyms` 的 `__crc_*` 生成的 `Module.symvers` 编译，`CONFIG_LOCALVERSION=-android16-5-zte-1006-4k` 对齐 vermagic |
| `ondevice.sh` | 设备侧一次采集：版本/config/CRCs/`resource*`/debugfs/`show_hba`/dmesg |

## 设备回来后的顺序

1. **先试零模块路线**：`mount -t debugfs none /sys/kernel/debug` 后 `cat …/ufshcd0/show_hba`；
   若 `/sys/devices/platform/soc/1d84000.ufshc/resource*` 可读，直接 `dd` 读 MCQ 窗口。
2. 取 `vermagic`（`modinfo` 或任取一个 .ko 的 `.modinfo`）与 `/proc/config.gz` 的
   `CONFIG_MODVERSIONS`、`CONFIG_MODULE_SIG*`，确定要不要签名。
3. 从 `/proc/kallsyms` 的 `__crc_*` 生成 `Module.symvers`；再从设备拉当前 `vendor_dlkm` 模块交叉校验 CRC。
4. 联网一次把 ACK 6.12 缓存进 `/opt/ghcache`，之后离线编译 `mcq_dump.ko`。
5. `insmod` 后 `dmesg | grep mcq_dump`，得到工作中配置；对照 `NativeUfs` 的接管序列逐项对齐。

## 风险

- `CONFIG_MODULE_SIG_FORCE=y` 时未签名模块无法加载 → 退步骤 1（debugfs/resource）。
- CRCs 必须与运行内核一致 → 只用本机 kallsyms，不用 6.12.23 的 .ko。
- 模块只读，不写任何寄存器；读 MCQ/SMMU 窗口不会触发硬件命令。
