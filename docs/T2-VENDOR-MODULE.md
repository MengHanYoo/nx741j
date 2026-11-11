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

## 7. T2 第一轮结果（2026-11-11 21:0x–21:4x，设备已在 Android）

### 设备恢复

- 原厂 efisp 写回 LBA 748894（3 MiB，读回 `sha256 4df183594a63af28` 一致）；
- 原厂 `boot_a` 写回 LBA 119078（96 MiB，读回 `sha256 3d988255d51fdaf7` 一致）；复位后 Android 正常启动，`adb root` 可用（KSU，uid=0）。

### 实测事实

| 项 | 值 | 含义 |
|---|---|---|
| `uname -r` / `/proc/version` | `6.12.38-android16-5-zte-1006-4k` | UTS 版本 |
| 所有 .ko 的 vermagic | **`6.12.23-maybe-dirty-4k SMP preempt mod_unload modversions aarch64`**（vendor_boot ramdisk 330 个、vendor_dlkm 300 个、system 103 个全一样） | 模块 ABI 是 **6.12.23** 源码树的 |
| 厂商模块 `.gnu.linkonce.this_module` | **0x640**（1600 B） | 内核 `struct module` 大小 |
| `CONFIG_MODVERSIONS` | `y` | 需要匹配 CRC |
| `CONFIG_MODULE_SIG` / `SIG_FORCE` | `y` / **未开** | 未签名模块允许加载（受 `SIG_PROTECT` 名单约束） |
| `CONFIG_KALLSYMS_ALL` | `y` | kallsyms 全符号 |
| 运行队列 | `nr_hw_queues=9`、`can_queue=63`、`cmd_per_lun=31`、`use_blk_mq=1` | **9 队列 blk-mq（MCQ）** |
| 调试入口 | 无 `/dev/mem`、无 `/proc/kcore`、无 `resource*`；debugfs 只有 `stats`（无 `show_hba`）；`kptr_restrict=2` | 只能走模块 |

### CRC 来源（关键进展）

- `/proc/kallsyms` 里 3,630 个 `__crc_*` **值全为 0**（`CONFIG_MODULE_REL_CRCS`）；
- **从 330 个厂商 .ko 的 `__versions` 段提取出 3,372 个 CRC**，已生成 `_tmp/t2/Module.symvers`：
  `_printk=0x16b5b21d`、`iounmap=0xfbb66790`、`module_layout=0xe976b219`、`__stack_chk_fail=0xd272d446`；
- **缺 `ioremap`**，但可用替代：`of_iomap=0xe2ff4068`、`devm_ioremap=0x6d7dac4a`、`of_find_compatible_node=0x730d7351`、`platform_bus_type=0x021aff95`。

### 模块加载实验（决定性证据）

`hello.ko`（6.12.38 源码 + 设备 `.config` + 3,372 CRC，vermagic 手工对齐 `6.12.38-android16-5-zte-1006-4k`）→
**vermagic 与签名都通过**，最后被内核拒绝：

```
module hello: .gnu.linkonce.this_module section size must match the kernel's built struct module size at run time
```

我们的 `.gnu.linkonce.this_module` 是 **0x500**，内核要 **0x640** → 必须用 **6.12.23 源码**（不是 6.12.38）。
6.12.23 源码已下载（148,058,060 B，`xz -t` 通过、树完整），用设备 `.config` 重建即可；本轮构建环境耗时未跑完。

### 结论与下一步

1. T2 的模块路径**可行且只剩最后一步**：6.12.23 + 设备 config + 3,372 CRC → `struct module` 0x640 → `insmod`；
2. `mcq_dump.c` 要把 `ioremap` 换成 **`of_iomap(of_find_compatible_node(NULL,NULL,"qcom,ufshc"), index)`**（index 0=ufs_mem，2=mcq_sqd，3=mcq_vs）；MCQCFG 在 `ufs_mem+0x300`，不需要单独的 mcq 窗口；
3. 拿到数据后对比 `NativeUfs` 的接管序列；已确认厂商是 **9 队列、depth 63、blk-mq**，这组参数直接写进我们的 MCQ 实现。
