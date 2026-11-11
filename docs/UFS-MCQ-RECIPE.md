# UFS MCQ 模式寄存器/内存布局配方（SM8850 / vendor 6.12.38）

> 目标：把本机厂商内核里 **UFS 主机控制器 MCQ（Multi-Circular Queue）模式**的软件流程
> 还原成一份可以直接照抄的寄存器/内存布局配方。
>
> 范围：**只做静态分析**。不修改任何驱动源码，不刷机，不写设备。
>
> 证据：`_tmp/vendor/vendor-kernel.elf`（stock `boot_a_20261102-1335.img` 提取，基址
> `0xffffffc080000000`，kernel `6.12.38-android16-5-zte-1006-4k`）+
> `_tmp/vbmod/lib/modules/ufs-qcom.ko`（同机 vendor_boot 模块，平台胶水层）。
> vmlinux SHA-256 `B99BD9AD…D995C5F1`；`ufs-qcom.ko` SHA-256 `30B5CAE7…D7E5CD68`。

---

## 0. 结论速查

| 项 | 本机值 | 依据 |
|---|---|---|
| UFSHCI MMIO | `0x1d84000`（窗口 `0x3000`） | DT `ufs_mem`；`NativeUfs` 既有代码 |
| MCQ 配置区 MCA | `0x1da0000`，窗口 `0x240` | `QCFGPTR=0xE0 × 0x200`；`/proc/iomem` 的 `mcq` |
| 每队列 MCA 步进 | `0x40` | `make_queues_operational` 的 `w23 += 0x40` |
| 队列数（本机） | 9（8 个 ESI + 1 个 poll） | `0x240 / 0x40 = 9`；`/proc/interrupts` 8 条 `qcom-mcq-esi` |
| SQ 操作区 | `0x1da5000 + i*0x100` | DT `mcq_sqd`；`ufs_qcom_op_runtime_config` |
| SQ 门铃（SQTP） | `0x1da5000 + i*0x100 + 0x04` | `hwq->[0x08] = opr_sqd + 4` |
| 门铃写入值 | `((tail+1) % max_entries) << 5` | `ufshcd_send_command` |
| SQE | 32 B，等同 legacy UTRD | 32 B `stp` 复制 |
| CQE | 32 B，DW0 非 0 表示有效 | `ufshcd_mcq_poll_cqe_lock` |
| MCQ 使能 | `MMIO+0x300 bit0` | `ufshcd_mcq_enable/disable` |
| MAC（最大活跃命令） | `MMIO+0x380 bits[16:8] = N-1` | `ufshcd_mcq_config_mac` |
| ESI | 可选；失败只打印 “ESI is not configured” | `ufshcd_config_mcq` 的调用点 |
| Legacy 门铃 | 代码保留，`MMIO+0x58`；但 `use_mcq_mode=0` 可能被拒绝 | `ufshcd_send_command` + 字符串 |

最小可用路径：**HCE=1 → make_hba_operational → 0x300 bit0=1 → 写 MCA 队列 0 →
写 0x380 MAC → 把 SQE 放进 SQ 槽 0 → 写 SQTP=0x20 → 轮询 CQTP/CQE**。
不需要 ESI，不需要 legacy 门铃。

---

## 1. 证据与方法

### 1.1 反汇编命令

```sh
# vmlinux（已重建符号）
llvm-nm -n vendor-kernel.elf | grep -E 'ufshcd_mcq|ufshcd_send_command|ufshcd_intr'
llvm-objdump -d --start-address=0xffffffc080cf1f7c \
                     --stop-address=0xffffffc080cf3400 vendor-kernel.elf

# 平台模块（ET_REL，地址是 .text 段内偏移）
llvm-nm -n ufs-qcom.ko | grep -E 'mcq|esi'
llvm-objdump -dr ufs-qcom.ko
```

### 1.2 关键符号（vmlinux，基址 `0xffffffc080000000`）

| 符号 | 地址 | 本文用途 |
|---|---|---|
| `ufshcd_mcq_config_mac` | `0xffffffc080cf1f7c` | MAC 字段 |
| `ufshcd_mcq_queue_cfg_addr` | `0xffffffc080cf20f4` | `QCFGPTR×0x200` |
| `ufshcd_mcq_decide_queue_depth` | `0xffffffc080cf2104` | 队列深度 |
| `ufshcd_mcq_memory_alloc` | `0xffffffc080cf21b4` | SQ/CQ DMA |
| `ufshcd_mcq_read_cqis` | `0xffffffc080cf2298` | 读 CQIS |
| `ufshcd_mcq_write_cqis` | `0xffffffc080cf22c8` | 写 CQIS |
| `ufshcd_mcq_compl_all_cqes_lock` | `0xffffffc080cf2300` | 清空 CQ |
| `ufshcd_mcq_poll_cqe_lock` | `0xffffffc080cf23d8` | 轮询 CQE |
| `ufshcd_mcq_make_queues_operational` | `0xffffffc080cf24dc` | 写 MCA |
| `ufshcd_mcq_enable_esi` | `0xffffffc080cf2758` | `0x300 bit1` |
| `ufshcd_mcq_enable` | `0xffffffc080cf279c` | `0x300 bit0=1` |
| `ufshcd_mcq_disable` | `0xffffffc080cf27ec` | `0x300 bit0=0` |
| `ufshcd_mcq_config_esi` | `0xffffffc080cf2838` | `0x384/0x388` |
| `ufshcd_mcq_init` | `0xffffffc080cf2884` | 分配 `uhq`、深度 |
| `ufshcd_mcq_sq_cleanup` | `0xffffffc080cf2b14` | SQ 清理 |
| `ufshcd_mcq_sq_stop` | `0xffffffc080cf2cf0` | SQRTC=1 |
| `ufshcd_mcq_sq_start` | `0xffffffc080cf2ddc` | SQRTC=0 |
| `ufshcd_mcq_abort` | `0xffffffc080cf2ed4` | 内联 `sqe_search` |
| `ufshcd_config_mcq` | `0xffffffc080ce5a04` | MCQ 配置总入口 |
| `ufshcd_alloc_mcq` | `0xffffffc080ce5c84` | 深度 + 内存 |
| `ufshcd_send_command` | `0xffffffc080ce2f1c` | MCQ/legacy 分支 |
| `ufshcd_compl_one_cqe` | `0xffffffc080cda42c` | OCS/response |
| `ufshcd_make_hba_operational` | `0xffffffc080cd9cf0` | HBA 运行 |
| `ufshcd_hba_enable` | `0xffffffc080cd9f50` | HCE |
| `ufshcd_init` | `0xffffffc080cddc50` | MCQ support/legacy 判定 |
| `ufshcd_device_init` | `0xffffffc080ce424c` | MCQ 初始化时序 |
| `ufshcd_intr` | `0xffffffc080cdff14` | MCQ 主中断路径 |

> 注意：本内核**没有**独立的 `ufshcd_mcq_write_cq` / `ufshcd_mcq_read_cq` /
> `ufshcd_mcq_sqe_search` / `ufshcd_mcq_irq` 符号。它们分别以
> `*_write_cqis` / `*_read_cqis`、内联进 `ufshcd_mcq_abort`、内联进
> `ufshcd_intr` 的形式存在。

### 1.3 平台模块符号（`ufs-qcom.ko`，`.text` 偏移）

| 符号 | 偏移 | 用途 |
|---|---|---|
| `ufs_qcom_mcq_config_resource` | `0x7780` | 映射 `mcq` / 计算 `mcq_base` |
| `ufs_qcom_get_hba_mac` | `0x7ae0` | 返回 64 |
| `ufs_qcom_op_runtime_config` | `0x7aec` | 填 `mcq_opr[]`：`0x21000/0x21040/0x21080/0x210c0`，步进 `0x100` |
| `ufs_qcom_get_outstanding_cqs` | `0x7b98` | 读 `mcq_vs+0x8` |
| `ufs_qcom_config_esi` | `0x7be8` | MSI + 每队列 IRQ |
| `ufs_qcom_mcq_esi_handler` | `0xbf58` | CQIS=1 + poll |

> 模块 vermagic 为 `6.12.23-maybe-dirty-4k`，比 vmlinux 的 `6.12.38` 旧；但模块
> 写入的 `hba+0x13c8`（`mcq_base`）、`0x13e0/0x13e8/0x13f0`（opr[0]）、
> `0x1308/0x1314`（队列数）与 vmlinux 的读取偏移逐一对上，说明同一份 `ufs_hba`
> 布局。本文把模块当作**同机平台胶水**使用，核心流程仍以 vmlinux 为准。

---

## 2. 地址空间总览

```
0x1d84000  UFSHCI MMIO（0x3000）
0x1da0000  MCA（MCQ Configuration Area，0x240 = 9 × 0x40）
0x1da4000  mcq_vs（0x10；CQIS_VS 在 +0x8）
0x1da5000  mcq_sqd（0x2000；每队列 0x100 的 4 个操作子区）
            = mcq_base + 0x5000 = MMIO + 0x21000
```

`mcq_base` 的计算有三条互相印证的路径：

1. **通用公式**：`mcq_base = MMIO + (MCQCAP[23:16] << 9)`。
   - `ufshcd_mcq_queue_cfg_addr`：`0xffffffc080cf20f4  ldrb w8,[x0,#0x96]`，
     `0xffffffc080cf20f8  lsl w0,w8,#9`。
   - QEMU 变体：`0xffffffc080cf4fd8  ldr x20,[x0]`（MMIO），
     `0xffffffc080cf4fe0  bl ufshcd_mcq_queue_cfg_addr`，
     `0xffffffc080cf4fe4  add x8,x20,w0,uxtw`，
     `0xffffffc080cf4fec  str x8,[x19,#0x13c8]`（`hba->mcq_base`）。
2. **QCOM 模块回退路径**：DT 没有 `mcq` 节点时，用 `ufs_mem.start + QCFGPTR<<9`
   造一个 `nr_queues*0x40` 的假 resource 再 ioremap：
   - `ufs-qcom.ko+0x794c  ldr x8,[x20]`（`ufs_mem` resource start）
   - `+0x7950  ldrb w9,[x19,#0x96]`（`QCFGPTR`）
   - `+0x7960  add x8,x8,x9,lsl#9`
   - `+0x7964  lsl w9,w10,#6` / `+0x796c  sub x9,x9,#1`
   - `+0x7970  stp x8,x9,[x0]`（假 resource 的 start/end）
   - `+0x79b8  str x2,[x19,#0x13c8]`（`hba->mcq_base`）
3. **实测**：`/proc/iomem` 的 `1da0000-1da023f mcq` 正好是 `0x240`，即 9 个
   `0x40` 队列；`1da5000-1da6fff mcq_sqd`、`1da4000 mcq_vs` 也吻合。

**本机推断**：`QCFGPTR = 0xE0`（`0xE0 × 0x200 = 0x1C000`），
`nr_hw_queues = 9`。

---

## 3. 寄存器表

> 说明：下表“偏移”均相对 `MMIO = 0x1d84000`，除非另注。读改写用 32-bit。
> 证据列给的是**最小充分指令**，不是全部。

### 3.1 UFSHCI 全局寄存器

| 偏移 | 名称 | 位域 | 含义 | 证据（地址 + 指令） |
|---|---|---|---|---|
| `0x000` | CAP | `[30]` MCQ support；`[7:0]` NUTRS-1 | 能力 | `0xcde15c ubfx w10,w9,#30,#1` |
| `0x004` | **MCQCAP** | `[7:0]` MAXQ-1；`[23:16]` QCFGPTR；`[10]` 保留/标志 | MCQ 能力 | `0xcde17c add x0,x8,#0x4`；`0xcde18c str w0,[x19,#0x94]` |
| `0x020` | IS | `[20]` MCQ CQ event | 中断状态 | `0xce055c add x0,x8,#0x20` |
| `0x024` | IE | 见下 | 中断使能 | `0xcd9d08 add x0,x8,#0x24` |
| `0x030` | HCS | `[3:1]` UTRLRDY/UTMRLRDY/UCRDY | 就绪 | `0xcd9db0 add x0,x8,#0x30`；`0xcd9db4 mov w8,#0xe` |
| `0x034` | HCE | `[0]` HCE | 使能 | `0xcd9f9c add x0,x9,#0x34` |
| `0x04c` | 厂商队列配置 | `(nutrs<<8)&0x1f00 \| 0x81000002` | ZTE/QCOM 私有 | `0xcd9d38`–`0xcd9d54` |
| `0x050/0x054` | UTRLBA/U | legacy UTRL 基址 | legacy | `0xcd9d70/0xcd9d80` |
| `0x058` | UTRLDBR | `1<<tag` | legacy 门铃 | `0xce3120 add x1,x8,#0x58` |
| `0x060` | UTRLRSR | `[0]=1` | legacy run | `0xcd9dd8 add x1,x8,#0x60` |
| `0x070/0x074` | UTMRLBA/U | legacy UTMR 基址 | legacy | `0xcd9d90/0xcd9da0` |
| `0x080` | UTMRLRSR | `[0]=1` | legacy run | `0xcd9dc8 add x1,x8,#0x80` |
| `0x300` | **MCQCFG** | `[0]` Enable；`[1]` ESI Enable | MCQ 总开关 | `0xcf27b4 add x0,x8,#0x300`；`0xcf27c0 orr w0,w0,#1`；`0xcf277c orr w0,w0,#2` |
| `0x380` | **MCQ MAC** | `[16:8]` = N-1（N=最大活跃命令） | 限流 | `0xcf1f98 add x0,x8,#0x380`；`0xcf1fac bfi w0,w8,#8,#9` |
| `0x384` | ESI LBA | MSI address low | ESI | `0xcf2858 add x1,x8,#0x384` |
| `0x388` | ESI UBA | MSI address high | ESI | `0xcf2868 add x1,x8,#0x388` |
| `0x271C` | QCOM CFG3 | `[22:12]` ESI vector count-1 | ESI 向量 | 模块 `0x7cc8 mov w20,#0x271c`；`0x7cd8 and w9,w0,#0xff800fff`；`0x7cdc orr w0,w9,#0x1f000` |

**IE 的两个值**

- `ufshcd_make_hba_operational`：`IE |= 0x700A85`
  （`0xcd9d14 mov w9,#0xa85`；`0xcd9d18 movk w9,#0x7,lsl #16`；`0xcd9d1c orr`）。
- `ufshcd_config_mcq`：`IE |= 0x17A884`（若 quirk bit20 则去掉 `0x100000`）
  （`0xce5a88 mov w9,#0xa84`；`0xce5a8c movk w9,#0x17,lsl #16`；
  `0xce5a90 and w8,w8,#0x100000`；`0xce5a98 eor w20,w8,w9`）。

### 3.2 MCA 每队列配置寄存器（相对 `mcq_base + 0x40*i`）

这些寄存器是**软件写、硬件读**的配置。QEMU 变体证明它们也可读回
（`0xcf502c/0xcf503c/0xcf504c/0xcf505c` 分别读 `+0x0c/+0x10/+0x2c/+0x30`）。

| 偏移 | 名称 | 位域 | 含义 | 证据 |
|---|---|---|---|---|
| `0x00` | **SQATTR** | `[31]` Enable；`[20:16]` CQ id；`[15:0]` SQ size（dword-1） | 提交队列属性 | `0xcf2540`–`0xcf254c` |
| `0x04` | SQLBA | `[31:0]` SQ 基址低 | | `0xcf2584`–`0xcf258c` |
| `0x08` | SQUBA | `[31:0]` SQ 基址高 | | `0xcf2590`–`0xcf25a0` |
| `0x0C` | **SQDAO** | `[31:0]` SQ 门铃地址偏移（相对 MMIO） | | `0xcf25a4`–`0xcf25bc` |
| `0x10` | SQISAO | `[31:0]` SQ 中断状态地址偏移 | | `0xcf25c0`–`0xcf25d8` |
| `0x20` | **CQATTR** | `[31]` Enable；`[15:0]` CQ size（dword-1） | 完成队列属性 | `0xcf2534`–`0xcf253c` |
| `0x24` | CQLBA | `[31:0]` CQ 基址低 | | `0xcf25dc`–`0xcf25ec` |
| `0x28` | CQUBA | `[31:0]` CQ 基址高 | | `0xcf25f0`–`0xcf2600` |
| `0x2C` | **CQDAO** | `[31:0]` CQ 门铃地址偏移 | | `0xcf2604`–`0xcf261c` |
| `0x30` | CQISAO | `[31:0]` CQ 中断状态地址偏移 | | `0xcf2620`–`0xcf2638` |

> 注意：本机把 `SQDAO`/`CQDAO` 当 **32-bit 偏移**；`+0x10` 是 `SQISAO`、`+0x30` 是
> `CQISAO`，**不是** 64-bit 门铃地址的高半部。上游 `ufsmcq_writelx` 的 64-bit 写法
> 在这里不适用（`ufs_qcom_op_runtime_config` 也是按 4 个独立 32-bit 偏移填的）。

**size 的编码**（关键，容易抄错）：

```
max_entries   = nutrs + 1                 // ufshcd_mcq_init: [hba+0x8c] + 1
qsize_dwords  = max_entries * 8 - 1       // 8 个 dword = 32 B
SQATTR        = 0x80000000 | (i << 16) | qsize_dwords
CQATTR        = 0x80000000 | qsize_dwords
```

证据：
- `0xcf2a04 ldr w9,[x19,#0x8c]`；`0xcf2a14 add w9,w9,#1`；`0xcf2a1c str w9,[x8,#0x40]`
  （`hwq->max_entries = nutrs + 1`）。
- `0xcf2520 add w8,w25,w20,lsl #3`（`0xffff + max_entries*8`）；
  `0xcf2530 bfxil w20,w8,#0,#16`（取低 16 位 = `max_entries*8-1`）。
- `0xcf2528 mov w20,#0x80000000`；`0xcf2544 orr w0,w20,w21`（`w21 = i<<16`）；
  `0xcf2540/0xcf254c` 分别写 `+0x20` 与 `+0x00`。

### 3.3 操作区寄存器（相对 `SQD_Q(i) = 0x1da5000 + 0x100*i`）

| 偏移 | 名称 | 读写 | 含义 | 证据 |
|---|---|---|---|---|
| `+0x000` | SQHP | RO | SQ head，字节偏移；`>>5` 得槽号 | `0xcf2f4c ldr x0,[x20]`；`0xcf2f58 lsr w9,w0,#5` |
| `+0x004` | **SQTP** | WO | SQ tail 门铃，写下一槽的字节偏移 | `0xce3080 ldr x1,[x21,#0x8]`；`0xce3098 bl writel` |
| `+0x008` | SQRTC | RW | `0`=start，`1`=stop，`bit1`=ICU（清理） | `0xcf2e08`（写 0）/ `0xcf2d1c`（写 1） |
| `+0x00C` | SQCTI | WO | nexus = `(iid<<16) \| (lun<<8) \| tag` | `0xcf2b90`–`0xcf2ba8` |
| `+0x010` | SQRTS | RO | `bit0`=run/stop 状态；`bit1`=cleanup done；`[7:4]`=错误码 | `0xcf2bd0 tbnz w0,#1`；`0xcf2c78 ubfx w6,w0,#4,#4` |
| `+0x040` | SQIS | RW | SQ 中断状态 | 由 SQISAO 指定 |
| `+0x080` | CQHP | WO | CQ head，字节偏移 | `0xcf2498 lsl w0,w8,#5`；`0xcf249c ldr x1,[x19,#0x10]` |
| `+0x084` | CQTP | RO | CQ tail，字节偏移；`>>5` 得槽号 | `0xcf2400 ldr x8,[x19,#0x18]`；`0xcf2414 lsr w8,w0,#5` |
| `+0x0C0` | CQIS | RW | `bit0` = tail entry push status | `0xcf22a4 ldr x8,[x0,#0x1430]`；`0xcf22b0 madd` |
| `+0x0C4` | CQIE | RW | CQ 中断使能 | `0xcf26b0 add x1,x8,#0x4` |

> 注意：`+0x040/+0x080/+0x0C0` 是**同一 0x100 块内的子区**，不是三个独立队列；
> `SQD_Q(i)` 是块基址。

### 3.4 位域速查（最容易出错的三处）

1. **`0x300`**：`bit0` 是 MCQ 总开关；`bit1` 是 ESI 使能。不要把它和 `0x380` 混淆。
2. **`0x380[16:8]`**：`N-1`（N = 最大活跃命令数，`nutrs`），不是队列深度本身。
   `bfi` 的 9 位宽正好容纳 0..511。
3. **SQATTR/CQATTR 的 size**：单位是 **dword**（4 B），且是 `max_entries*8-1`；
   不是字节数，也不是 `max_entries`。

---

## 4. 内存布局图（文本）

### 4.1 物理窗口

```
0x1d84000 ┌──────────────────────────────────────────────┐
          │ UFSHCI MMIO 0x3000                           │
          │  +0x000 CAP        +0x004 MCQCAP             │
          │  +0x024 IE         +0x034 HCE                │
          │  +0x050 UTRLBA     +0x058 UTRLDBR (legacy)   │
          │  +0x300 MCQCFG     +0x380 MCQ MAC            │
          │  +0x384/0x388 ESI  +0x271C CFG3              │
0x1d87000 └──────────────────────────────────────────────┘
0x1da0000 ┌──────────────────────────────────────────────┐
          │ MCA (mcq) 0x240 = 9 × 0x40                   │
          │  q0: +0x00 SQATTR ... +0x30 CQISAO           │
          │  q1: +0x40 ...                               │
          │  q2: +0x80 ...                               │
          │  ...                                         │
          │  q8: +0x200 ... +0x23F                       │
0x1da4000 ├──────────────────────────────────────────────┤
          │ mcq_vs 0x10                                  │
          │  +0x008 CQIS_VS（每个 bit 一个队列）          │
0x1da5000 ├──────────────────────────────────────────────┤
          │ mcq_sqd 0x2000（每队列 0x100）                │
          │  q0: +0x000 SQD   +0x040 SQIS                │
          │      +0x080 CQD   +0x0C0 CQIS                │
          │  q1: +0x100 ...                              │
          │  ...                                         │
          │  q8: +0x800 ...                              │
0x1da7000 └──────────────────────────────────────────────┘
```

### 4.2 每队列 0x100 块（以 q0 为例）

```
0x1da5000  +0x000  SQHP    (RO, byte offset)
0x1da5004  +0x004  SQTP    (WO, doorbell)
0x1da5008  +0x008  SQRTC   (start/stop/ICU)
0x1da500C  +0x00C  SQCTI   (nexus)
0x1da5010  +0x010  SQRTS   (status)
0x1da5040  +0x040  SQIS
0x1da5080  +0x080  CQHP    (WO)
0x1da5084  +0x084  CQTP    (RO)
0x1da50C0  +0x0C0  CQIS
0x1da50C4  +0x0C4  CQIE
```

### 4.3 驱动 DMA 内存

```
SQ ring（每个队列一份）
  max_entries = nutrs + 1
  每项 32 B（SQE = UTRD）
  ┌──────────┬──────────┬─────┬──────────┐
  │ SQE[0]   │ SQE[1]   │ ... │ SQE[N]   │
  └──────────┴──────────┴─────┴──────────┘
  SQ/CQ 基址建议 4 KB 对齐；UCD 基址必须 128 B 对齐（CQE DW0 低 7 位被忽略）

CQ ring（每个队列一份）
  每项 32 B（CQE）
  ┌──────────┬──────────┬─────┬──────────┐
  │ CQE[0]   │ CQE[1]   │ ... │ CQE[N]   │
  └──────────┴──────────┴─────┴──────────┘
  有效项判定：DW0 != 0；处理完写 DW0 = 0

UCD（每个 tag 一份；最小实现一份即可）
  +0x000  命令 UPIU（512 B）
  +0x200  响应 UPIU（512 B）
  +0x400  PRDT（每个 16 B）
  总大小 = 0x400 + sg_tablesize*0x80（本机 ucd_size 由代码计算）
```

证据：
- `ufshcd_mcq_memory_alloc`：`0xcf2204 ldr w8,[x24,#0x40]`（`max_entries`），
  `0xcf220c lsl x1,x8,#5`（×32），`0xcf2210 bl dmam_alloc_attrs`，
  `0xcf2218 str x0,[x24,#0x20]`（SQ VA），`0xcf221c`（SQ DMA 在 `+0x28`）。
- 响应 UPIU 偏移：`0xce5e08 add x21,x2,#0x200`；`0xce5e10 stp x2,x21,[x3,#8]`
  （`lrb->ucd = UCD`，`lrb->ucd_rsp = UCD+0x200`）。
- PRDT 偏移：`0xce5e18 add x2,x2,#0x400`；`0xce5e28 stp x2,x4,[x3,#0x18]`。
- PRDT 在 UTRD 中的长度：`0xce7104 ldr x10,[x22]`；`0xce7110 strh w9,[x10,#0x1c]`。

### 4.4 本机推断值（用于“照抄”）

| 量 | 值 | 备注 |
|---|---|---|
| `MMIO` | `0x1d84000` | |
| `MCA` | `0x1da0000` | `QCFGPTR=0xE0` |
| `SQD` | `0x1da5000` | |
| `VS` | `0x1da4000` | |
| `nr_hw_queues` | 9 | 8 ESI + 1 poll；窗口 `0x240` |
| `N = nutrs` | `min(64, bQueueDepth)` | QCOM `get_hba_mac` 返回 64 |
| `max_entries` | `N+1` | |
| `SQATTR/CQATTR size` | `(N+1)*8-1` | |
| `MAC` | `N-1` | |

---

## 5. 数据结构

### 5.1 SQE（32 B）= legacy UTRD

`ufshcd_send_command` 的 MCQ 分支不重新编码，而是把 `lrb->utrd` 指向的
**32 字节原样复制**到 SQ 槽：

- `0xce3050 ldr x19,[x20]`（`lrb->utr_descriptor_ptr`）
- `0xce305c ldr x8,[x21,#0x20]`（`hwq->sqe_base_addr`）
- `0xce3060 ldr w9,[x21,#0x48]`（`hwq->sq_tail_slot`）
- `0xce3068 add x8,x8,x9,lsl #5`（`+ slot*32`）
- `0xce306c ldp x9,x10,[x19,#0x10]`
- `0xce3070 stp x11,x12,[x8]`
- `0xce3074 stp x9,x10,[x8,#0x10]`

因此 SQE 的字段与 legacy UTRD **完全一致**，只是放置位置和门铃不同。

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| `0x00` | 1 | `cci` | 命令控制信息；普通 SCSI 为 0 |
| `0x01` | 1 | `ehs_length` | 通常 0 |
| `0x02` | 1 | `enable_crypto:1` + reserved:7 | `bit7` = 内联加密 |
| `0x03` | 1 | `interrupt:1` + `data_direction:2` + reserved:1 + `command_type:4` | `bit0` 中断；`bits2:1` 方向（1=读，2=写）；`bits7:4` 命令类型（0=SCSI，0xF=空操作） |
| `0x04` | 4 | `dunl` | |
| `0x08` | 1 | `ocs` | 初始化为 `0xF` |
| `0x09` | 1 | `cds` | |
| `0x0A` | 2 | `ldbc` | |
| `0x0C` | 4 | `dunu` | |
| `0x10` | 8 | **`command_desc_base_addr`** | UCD 的 DMA 地址；硬件忽略低 7 位（`& ~0x7F`），低 5 位可放 SQ id |
| `0x18` | 2 | `response_upiu_length` | 响应缓冲区长度（通常 512） |
| `0x1A` | 2 | `response_upiu_offset` | 固定 `0x200` |
| `0x1C` | 2 | `prdt_length` | PRD 条目数 |
| `0x1E` | 2 | `prdt_offset` | 固定 `0x400` |

关键证据：
- **UCD 指针在 +0x10**：`0xce5da4 str x1,[x2,#0x10]`（`x2` 是 UTRD，`x1` 是 UCD DMA）。
- **OCS 初始化 0xF**：`0xce6f5c mov w11,#0xf`；`0xce6f68 strb w11,[x8,#8]`。
- **方向/中断位**：`0xce6f10 lsl w11,w11,#9`（方向）；
  `0xce6f2c orr w11,w11,#0x100`（中断，仅在 `lrb+0x55==1` 时）；
  `0xce6f1c strh w11,[x8,#2]`。
- **command_type 清零/置 0xF**：`0xcf302c ldrh w8,[x12,#2]`；
  `0xcf3038 orr w8,w8,#0xf000`；`0xcf303c strh w8,[x12,#2]`。
- **PRDT 长度在 +0x1C**：`0xce7104 ldr x10,[x22]`；`0xce7110 strh w9,[x10,#0x1c]`。

#### 与 legacy UTRD 的差异

| 项 | legacy | MCQ |
|---|---|---|
| 描述符 | 同一个 32 B UTRD | 同一个 32 B UTRD |
| 放置 | UTRL 环 `utrdl_base + tag*32` | 每队列 SQ 环 `sqe_base + slot*32` |
| 索引 | task tag（全局） | 队列内 slot（`tail`） |
| 门铃 | `MMIO+0x58 = 1<<tag` | `SQD_Q(i)+4 = ((tail+1)%max_entries)<<5` |
| 完成 | UTRD 内存里的 OCS + UTRL 中断 | 每队列 CQ 的 32 B CQE |
| tag 来源 | 门铃位图 | CQE DW0 的 UCD 地址反推 |

### 5.2 CQE（32 B）

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| `0x00` | 8 | `command_desc_base_addr` | UCD DMA，低位含 SQ id；**非 0 即有效** |
| `0x08` | 2 | `response_upiu_length` | |
| `0x0A` | 2 | `response_upiu_offset` | |
| `0x0C` | 2 | `prdt_length` | |
| `0x0E` | 2 | `prdt_offset` | |
| `0x10` | 4 | `status` | **OCS = bits[3:0]** |
| `0x14` | 12 | reserved | |

完成判定与 tag 反推（`ufshcd_mcq_poll_cqe_lock`）：

- 读 CQTP：`0xcf2400 ldr x8,[x19,#0x18]`；`0xcf240c bl readl`；
  `0xcf2414 lsr w8,w0,#5`（字节偏移 → 槽号）。
- 有效判定：`0xcf2458 ldr x10,[x22]`；`0xcf245c cbz x10,...`（DW0 为 0 跳过）。
- tag 反推：`0xcf2468 and x10,x10,#0xffffffffffffff80`；
  `0xcf2478 sub x9,x10,x9`（减 `hba->ucdl_dma_addr`）；
  `0xcf2484 bl ufshcd_compl_one_cqe`。
- 清空：`0xcf2488 str xzr,[x22]`。
- CQHP 回写：`0xcf2498 lsl w0,w8,#5`；`0xcf24a4 bl writel`。

`ufshcd_compl_one_cqe` 里 OCS 的取值：
`0xcda4e4`–`0xcda524`：
```
ldr w8,[x21,#0x10]   ; cqe->status
and w2,w8,#0xf       ; OCS
```
响应 UPIU 不在 CQE 里，而是在 UCD 的 `+0x200`：
`0xcda590 ldr x8,[x20,#0x10]`（`lrb->ucd_rsp`），
`0xcda594 ldrb w9,[x8]`（transaction type，应为 `0x21`），
`0xcda5a0 ldrb w23,[x8,#7]`（response code）。

### 5.3 UCD（命令描述符）

```
+0x000  命令 UPIU（512 B）
+0x200  响应 UPIU（512 B）
+0x400  PRDT
```

- `lrb+0x08` = 命令 UPIU 指针，`lrb+0x10` = 响应 UPIU 指针
  （`0xce5e10 stp x2,x21,[x3,#8]`）。
- `lrb+0x18` = PRDT 指针（`0xce5e28 stp x2,x4,[x3,#0x18]`）。
- `command_desc_base_addr` 必须 128 B 对齐（低 7 位被硬件忽略）。

### 5.4 软件队列控制块（`struct ufs_hw_queue`，0x90）

vmlinux 的 `hba->uhq` 每项 0x90（`0xcf29d0 lsl x1,x8,#4`，`x8 = nr*9`），
字段偏移如下（供对照，不必照抄）：

| 偏移 | 字段 | 证据 |
|---|---|---|
| `0x00` | `mcq_sq_head`（SQD+0 的 VA） | `0xcf264c`–`0xcf265c` |
| `0x08` | `mcq_sq_tail`（SQD+4 的 VA） | 同上 |
| `0x10` | `mcq_cq_head`（CQD+0 的 VA） | `0xcf2660`–`0xcf266c` |
| `0x18` | `mcq_cq_tail`（CQD+4 的 VA） | `0xcf2670`–`0xcf2688` |
| `0x20` | `sqe_base_addr`（VA） | `0xcf2218` |
| `0x28` | `sqe_dma_addr` | `0xcf221c` |
| `0x30` | `cqe_base_addr`（VA） | `0xcf223c` |
| `0x38` | `cqe_dma_addr` | `0xcf2240` |
| `0x40` | `max_entries` | `0xcf2a1c` |
| `0x44` | `id` | `0xcf2578 str w22,[x27,#0x44]` |
| `0x48` | `sq_tail_slot` | `0xce3060` |
| `0x4C` | `sq_lock` | `0xce3054 add x0,x21,#0x4c` |
| `0x50` | `cq_tail_slot` | `0xcf241c` |
| `0x54` | `cq_head_slot` | `0xcf2418` |
| `0x58` | `cq_lock` | `0xcf23f4 add x0,x1,#0x58` |
| `0x60` | `sq_mutex` | `0xcf2b60 add x0,x20,#0x60` |

---

## 6. 使能顺序：HCE=1 → 第一条命令

以下顺序来自 `ufshcd_init` → `ufshcd_probe_hba` → `ufshcd_device_init` 的实际调用链。
“证据”只列关键指令。

### 步骤 0：读能力

1. 读 `0x000` CAP，检查 `bit30`（MCQ support）。
   - `0xcde15c ubfx w10,w9,#30,#1`；`0xcde16c strb w10,[x25,#0x31b]`。
2. 读 `0x004` MCQCAP，得到 `MAXQ = [7:0]+1`、`QCFGPTR = [23:16]`。
   - `0xcde17c add x0,x8,#0x4`；`0xcde180 bl readl`；
   - `0xcde18c str w0,[x19,#0x94]`；
   - `0xcf28b0 ldrb w11,[x0,#0x94]`；`0xcf28c0 add w20,w11,#1`（队列数）。
3. 计算 `mcq_base = MMIO + (QCFGPTR << 9)`。
   - `0xcf20f4/0xcf20f8`；或模块 `0x7960 add x8,x8,x9,lsl#9`。

### 步骤 1：HCE=1

`ufshcd_hba_enable`：

- 若 `0x034 bit0` 已为 1，先 `ufshcd_hba_stop`（`0xcd9f9c`–`0xcd9fac`）。
- `0xcd9fec bl ufshcd_crypto_enable`。
- 写 `0x034 = 3`（crypto 成功）或 `1`：
  `0xcd9ff0`–`0xcda000`（`csinc w0,w21,wzr,ne`，`w21=3`）。
- 轮询 `0x034 bit0 == 1`：`0xcda034`/`0xcda060`。
- 成功后 `IE |= 0x470`：`0xcda13c`–`0xcda158`。

> 这一步只要求 HCE=1。若固件已经把链路带起来，可以跳过 UIC 链路启动；
> 但若要完整冷启动，按既有 `docs/UFS-VENDOR-DIFF.md` 的 UIC 流程先做 link startup。

### 步骤 2：make_hba_operational

`ufshcd_make_hba_operational`（`0xffffffc080cd9cf0`）：

1. `IE |= 0x700A85`：`0xcd9d08`–`0xcd9d24`。
2. 可选厂商寄存器 `0x04C`：若 `hba[0xbb8] bit4` 且 `hba[0xd4] bit0==0`，
   写 `0x81000002 | ((nutrs<<8)&0x1f00)`，否则写 0：
   - `0xcd9d38 ldr w8,[x19,#0x8c]`
   - `0xcd9d3c mov w9,#0x1f00`
   - `0xcd9d40 add w8,w9,w8,lsl #8`
   - `0xcd9d4c and w8,w8,#0x1f00`
   - `0xcd9d50 orr w0,w8,w9`（`w9=0x81000002`）
   - `0xcd9d54 b ...` / `0xcd9d64 bl writel`（`+0x4c`）
3. `UTRLBA = [hba+0x28]/[0x2c]`：`0xcd9d70`/`0xcd9d80`。
4. `UTMRLBA = [hba+0x30]/[0x34]`：`0xcd9d90`/`0xcd9da0`。
5. 读 `HCS(0x30)`，要求 `& 0xe == 0xe`（`0xcd9db4 mov w8,#0xe`；
   `0xcd9db8 bics`）。
6. `UTMRLRSR(0x80)=1`：`0xcd9dc8`；`UTRLRSR(0x60)=1`：`0xcd9dd8`。

> 在 MCQ 模式，UTRL/UTMRL 环不被使用，但厂商仍然做这套（并且要求 HCS 三位 ready）。
> 最小实现可以保留一个 dummy UTRL/UTMRL 缓冲，或直接跳过第 3–6 步——
> 前提是 HCS 已经 ready 且你确定硬件不检查 run 位。

### 步骤 3：MCQ 总开关

`ufshcd_mcq_enable`：

```
r = readl(MMIO+0x300);
writel(r | 1, MMIO+0x300);
hba->mcq_enabled = 1;          // hba+0x131d
```

证据：`0xcf27b4 add x0,x8,#0x300`；`0xcf27c0 orr w0,w0,#1`；
`0xcf27d4 strb w9,[x19,#0x131d]`。

> 厂商首次初始化顺序是 **enable → alloc → config**：
> `0xce518c bl ufshcd_mcq_enable`，`0xce5194 bl ufshcd_alloc_mcq`，
> `0xce51a0 bl ufshcd_config_mcq`。此时队列 ATTR 的 enable 位还是 0，
> 所以先开总开关不会让硬件取队列。

### 步骤 4：分配队列与内存

`ufshcd_alloc_mcq`（`0xffffffc080ce5c84`）：

1. `old = hba->nutrs`（`0xce5c98 ldr w20,[x0,#0x8c]`）。
2. `hba->nutrs = ufshcd_mcq_decide_queue_depth(hba)`：
   - `0xce5ca0 bl`；`0xce5ca8 str w0,[x19,#0x8c]`。
   - `decide_queue_depth`：有 `vops->get_hba_mac` 就用它（QCOM 返回 64），
     否则 `CAP[7:0]+1`；最后 `min(mac, dev_info.bqueuedepth)`。
3. `ufshcd_mcq_init(hba)`：`0xce5cb0 bl`。
   - `vops->mcq_config_resource`（映射 `mcq`）：
     `0xcf2960 ldr x8,[x8,#0xc8]`；`0xcf2984 blr x8`。
   - `vops->op_runtime_config`（填 `mcq_opr[]`）：
     `0xcf2994 ldr x8,[x8,#0xd8]`；`0xcf29b8 blr x8`。
   - `devm_kmalloc(nr_hw_queues * 0x90)`：`0xcf29c4`–`0xcf29d8`。
   - 每队列 `max_entries = nutrs+1`：`0xcf2a04`–`0xcf2a1c`。
   - `hba->dev_cmd_queue = &uhq[0]`：`0xcf2ab4 str x8,[x19,#0x13d8]`。
   - `host->host_tagset = 1`：`0xcf2abc`–`0xcf2ac4`。
4. `ufshcd_mcq_memory_alloc(hba)`：`0xce5cc8 bl`。
   - 每队列 `dmam_alloc_attrs(max_entries*32)` 两次（SQ、CQ）：
     `0xcf2204`–`0xcf2240`。

### 步骤 5：写 MCA（`ufshcd_config_mcq`）

`ufshcd_config_mcq`（`0xffffffc080ce5a04`）：

1. **可选 ESI**：`vops->config_esi(hba)`：
   - `0xce5a2c ldr x8,[x8,#0xe8]`；`0xce5a5c blr x8`；
   - `0xce5a60 cmp w0,#0`；`0xce5a6c csel x2,...`（只影响日志，
     `0xce5a70 bl _dev_info` 的 “ESI %sconfigured”）。
   - **失败不阻断**：后面继续执行。
2. **IE |= 0x17A884**：`0xce5a80`–`0xce5aac`。
3. **`ufshcd_mcq_make_queues_operational`**：`0xce5ab4 bl`。
   对每个队列 i（`w23 = 0x20 + 0x40*i`）：
   - `+0x04 = lo32(sqe_dma)`：`0xcf2584`–`0xcf258c`
   - `+0x08 = hi32(sqe_dma)`：`0xcf2590`–`0xcf25a0`
   - `+0x0C = opr[0].offset + i*0x100`：`0xcf25a4`–`0xcf25bc`
   - `+0x10 = opr[1].offset + i*0x100`：`0xcf25c0`–`0xcf25d8`
   - `+0x24 = lo32(cqe_dma)`：`0xcf25dc`–`0xcf25ec`
   - `+0x28 = hi32(cqe_dma)`：`0xcf25f0`–`0xcf2600`
   - `+0x2C = opr[2].offset + i*0x100`：`0xcf2604`–`0xcf261c`
   - `+0x30 = opr[3].offset + i*0x100`：`0xcf2620`–`0xcf2638`
   - `+0x20 = CQATTR = 0x80000000 | qsize`：`0xcf2520`–`0xcf253c`
   - `+0x00 = SQATTR = 0x80000000 | (i<<16) | qsize`：`0xcf2540`–`0xcf254c`
   - 非 poll 队列：`CQIS_Q(i)+0x4 = 1`：`0xcf26a0`–`0xcf26b4`
4. **MAC**：`ufshcd_mcq_config_mac(hba, hba->nutrs)`：
   - `0xce5ab8 ldr w1,[x19,#0x8c]`；`0xce5ac0 bl`。
   - `0xcf1f98 add x0,x8,#0x380`；`0xcf1fa0 mov w8,#0x1ff00`；
     `0xcf1fa4 add w8,w8,w20,lsl #8`；`0xcf1fa8 lsr w8,w8,#8`；
     `0xcf1fac bfi w0,w8,#8,#9`；`0xcf1fb8 bl writel`。
5. 打印 `MCQ configured, nr_queues=%d, ...`：
   `0xce5ae4 bl _dev_info`。

### 步骤 6：可选 ESI

QCOM 模块 `ufs_qcom_config_esi`（`ufs-qcom.ko+0x7be8`）：

1. `nr_irqs = nr_hw_queues - poll_queues`：
   `+0x7c1c ldr w8,[x19,#0x1308]`；`+0x7c20 ldr w9,[x19,#0x1314]`；
   `+0x7c30 sub w1,w8,w9`。
2. `platform_device_msi_init_and_alloc_irqs(dev, nr_irqs, write_msi_msg)`：
   `+0x7c34`。
3. 每队列 `devm_request_irq(..., "qcom-mcq-esi")`：
   `+0x7c70`–`+0x7d18`。
4. `hw_ver.major >= 6` 时写 `0x271C`：
   `+0x7cb8 cmp w8,#6`；`+0x7cc8 mov w20,#0x271c`；
   `+0x7cd8 and w9,w0,#0xff800fff`；`+0x7cdc orr w0,w9,#0x1f000`。
5. `ufshcd_mcq_enable_esi(hba)`：`+0x7cec`；`0x300 bit1=1`。
6. `esi_enabled = 1`：`+0x7d24 strb w8,[x23,#0xc42]`。

### 步骤 7：第一条命令

见第 7、8 节。

---

## 7. 提交与完成

### 7.1 提交（MCQ 分支）

`ufshcd_send_command` 在 `hba+0x131d == 1` 时走 MCQ：

```
0xce3044  ldrb w8,[x22,#0x31d]      ; mcq_enabled
0xce3048  cmp  w8,#1
0xce304c  b.ne legacy               ; 0xce30a8
0xce3050  ldr  x19,[x20]            ; lrb->utrd
0xce3054  add  x0,x21,#0x4c         ; hwq->sq_lock
0xce3058  bl   _raw_spin_lock
0xce305c  ldr  x8,[x21,#0x20]       ; sqe_base
0xce3060  ldr  w9,[x21,#0x48]       ; sq_tail_slot
0xce3064  ldp  x11,x12,[x19]
0xce3068  add  x8,x8,x9,lsl #5      ; + slot*32
0xce306c  ldp  x9,x10,[x19,#0x10]
0xce3070  stp  x11,x12,[x8]
0xce3074  stp  x9,x10,[x8,#0x10]
0xce3078  ldr  w8,[x21,#0x48]       ; tail
0xce307c  ldr  w9,[x21,#0x40]       ; max_entries
0xce3080  ldr  x1,[x21,#0x8]        ; SQD+4
0xce3084  add  w10,w8,#1
0xce3088  cmp  w10,w9
0xce308c  csinc w8,wzr,w8,eq        ; wrap
0xce3090  lsl  w0,w8,#5             ; 门铃值
0xce3094  str  w8,[x21,#0x48]
0xce3098  bl   writel
```

要点：

- SQE 必须先在普通内存里构造好（`lrb->utrd` 是模板），复制到 SQ 槽后**清 cache**。
- 门铃值 = **下一槽**的字节偏移 = `((tail+1) % max_entries) << 5`。
- 写门铃前要保证 SQE 已经对设备可见（DSB/clean）。

### 7.2 完成（轮询）

`ufshcd_mcq_poll_cqe_lock`：

```
0xcf2400  ldr  x8,[x19,#0x18]       ; CQD+4 (CQTP)
0xcf240c  bl   readl
0xcf2410  ldr  w9,[x19,#0x54]       ; cq_head_slot
0xcf2414  lsr  w8,w0,#5             ; tail slot
0xcf2418  cmp  w9,w8
0xcf241c  str  w8,[x19,#0x50]       ; cq_tail_slot
0xcf2420  b.eq done
0xcf2454  add  x22,x10,x11,lsl #5   ; CQE = cqe_base + head*32
0xcf2458  ldr  x10,[x22]
0xcf245c  cbz  x10,next             ; DW0==0 -> 空
0xcf2468  and  x10,x10,#~0x7f
0xcf2478  sub  x9,x10,x9             ; - ucdl_dma
0xcf2484  bl   ufshcd_compl_one_cqe
0xcf2488  str  xzr,[x22]             ; 清空
0xcf2498  lsl  w0,w8,#5
0xcf249c  ldr  x1,[x19,#0x10]        ; CQD+0 (CQHP)
0xcf24a4  bl   writel
```

判定规则：

- **CQ 尾**：`CQTP >> 5`（CQTP 是字节偏移，不是槽号）。
- **有效 CQE**：`CQE.DW0 != 0`。
- **OCS**：`CQE[0x10] & 0xf`；0=成功。
- **响应 UPIU**：UCD+0x200；response code 在 byte 7。
- 处理完：`CQE.DW0 = 0`，`CQHP = head << 5`。

### 7.3 中断 / ESI

两条可用路径：

1. **主 UFS IRQ + CQIS_VS**（`ufshcd_intr`）：
   - `0xce04c8` 起：取 `nr_hw_queues - poll_queues`，构造 bitmap；
   - `0xce0518 bl ufshcd_mcq_read_cqis`（读 `CQIS_Q(i)`）；
   - `0xce0530 bl ufshcd_mcq_write_cqis`（回写同样值，ack）；
   - 若 `val & 1`：`0xce0540 bl ufshcd_mcq_poll_cqe_lock`。
   - 队列 bitmap 来自 `vops->get_outstanding_cqs`（QCOM 读 `mcq_vs+0x8`，
     模块 `0x7b98`：`ldr x8,[x0,#0x13c0]`；`add x0,x8,#0x8`；`bl readl`）。
2. **每队列 ESI**（QCOM 模块）：
   - `0xbfbc mov w1,#1`；`0xbfc8 bl ufshcd_mcq_write_cqis`；
   - `0xbfe4 bl ufshcd_mcq_poll_cqe_lock`。
   - ESI 使能：`0x300 bit1`；MSI 地址：`0x384/0x388`；向量数：`0x271C`。

**是否必需？**

- 不。`ufshcd_config_mcq` 调用 `config_esi` 后只用返回值选择日志字符串
  （`0xce5a60 cmp w0,#0`；`0xce5a6c csel`；`0xce5a70 dev_info`），
  不改变后续流程。
- 不配 ESI 也可以轮询 CQ（本配方的最小路径）。
- 配 ESI 只是降低延迟；QCOM 的 `config_esi` 失败时驱动继续。

### 7.4 错误恢复（可选）

- `ufshcd_mcq_sq_stop`：`SQRTC=1`，轮询 `SQRTS bit0==1`
  （写：`0xcf2d1c`/`0xcf2d28`；读：`0xcf2d44`）。
- `ufshcd_mcq_sq_start`：`SQRTC=0`，轮询 `SQRTS bit0==0`
  （写：`0xcf2e08`/`0xcf2e14`；读：`0xcf2e30`）。
- `ufshcd_mcq_sq_cleanup`：
  - `SQCTI = (iid<<16) | (lun<<8) | tag`：`0xcf2b90`–`0xcf2ba8`；
  - `SQRTC |= 2`（ICU）：`0xcf2bb4`–`0xcf2bbc`；
  - 轮询 `SQRTS bit1`：`0xcf2bd0`；
  - 完成后 `sq_start`：`0xcf2c8c`。
- `ufshcd_mcq_abort`（内联 `sqe_search`）：
  - `sq_stop` 后读 `SQHP >> 5`：`0xcf2f4c`–`0xcf2f5c`；
  - 从 head 扫到 tail，比较 `SQE[0x10] & ~0x7f`：`0xcf2f80`–`0xcf2f8c`；
  - 命中则 `SQE[2] |= 0xF000`（command_type=0xF）：`0xcf302c`–`0xcf303c`；
  - `sq_start` 恢复。

---

## 8. 最小可用提交序列（伪代码）

以下代码按本机常量写死；`N` 取 `min(64, bQueueDepth)`（本机常见 32）。
只用一个队列、一个 UCD、同步轮询。

**本节证据地址**：SQ 门铃 `0xce3080`–`0xce3098`；MCA 写入 `0xcf2584`–`0xcf2638`；
ATTR `0xcf2520`–`0xcf254c`；MAC `0xcf1fa0`–`0xcf1fb8`；CQ 轮询 `0xcf2400`–`0xcf24a4`；
CQE 有效/OCS `0xcf2458`、`0xcda4e8`–`0xcda524`；总开关 `0xcf27c0`/`0xcf27d4`。

```c
/* ---- 本机常量 ---- */
#define UFS_MMIO        0x1D84000UL
#define UFS_MCA         0x1DA0000UL
#define UFS_MCQ_SQD     0x1DA5000UL

#define R_CAP           0x000
#define R_MCQCAP        0x004
#define R_IE            0x024
#define R_HCE           0x034
#define R_MCQCFG        0x300
#define R_MCQMAC        0x380

#define MCA_Q(i)        (UFS_MCA + 0x40UL*(i))
#define SQD_Q(i)        (UFS_MCQ_SQD + 0x100UL*(i))
#define CQD_Q(i)        (SQD_Q(i) + 0x080UL)
#define CQIS_Q(i)       (SQD_Q(i) + 0x0C0UL)

#define R32(a)          (*(volatile uint32_t *)(a))
#define W32(a,v)        (*(volatile uint32_t *)(a) = (uint32_t)(v))

static void wait_bits_set(uint32_t addr, uint32_t mask)
{
    while ((R32(addr) & mask) != mask)
        ;
}

static void wait_bits_clr(uint32_t addr, uint32_t mask)
{
    while (R32(addr) & mask)
        ;
}

/* 只配置队列 0；n = 活跃命令数，max_entries = n+1 */
int mcq_setup_one_queue(uint32_t n,
                        void *sq_va, uint64_t sq_dma,
                        void *cq_va, uint64_t cq_dma,
                        void *ucd_va, uint64_t ucd_dma)
{
    uint32_t cap, mcqcap, qcfg, qsize;
    uint64_t mca;

    (void)ucd_va;   /* UCD 由 mcq_submit_slot0() 使用 */
    (void)ucd_dma;

    /* 0. 能力 */
    cap = R32(UFS_MMIO + R_CAP);
    if (!(cap & (1u << 30)))
        return -1;                         /* 没有 MCQ */
    mcqcap = R32(UFS_MMIO + R_MCQCAP);
    qcfg   = (mcqcap >> 16) & 0xFF;
    mca    = UFS_MMIO + ((uint64_t)qcfg << 9);
    if (mca != UFS_MCA)
        return -2;                         /* 与实测不符，先别继续 */

    qsize = (n + 1) * 8 - 1;

    /* 1. 先关 MCQ，保证队列不被取 */
    W32(UFS_MMIO + R_MCQCFG, R32(UFS_MMIO + R_MCQCFG) & ~1u);

    /* 2. HCE=1（如果还没开） */
    if (!(R32(UFS_MMIO + R_HCE) & 1u)) {
        W32(UFS_MMIO + R_HCE, 1);
        wait_bits_set(UFS_MMIO + R_HCE, 1);
    }

    /* 3. 可选：make_hba_operational 的最小版
     *    - IE |= 0x700A85
     *    - 若 HCS(0x30) 的 0xE 位已 ready，可直接置 run 位
     */
    W32(UFS_MMIO + R_IE, R32(UFS_MMIO + R_IE) | 0x00700A85u);

    /* 4. 厂商顺序：先开 MCQ 总开关；
     *    此时 SQATTR/CQATTR 的 enable 位还是 0，硬件不会取队列。
     */
    W32(UFS_MMIO + R_MCQCFG, R32(UFS_MMIO + R_MCQCFG) | 1u);

    /* 5. 清空 SQ/CQ 内存（DW0=0 表示空） */
    memset(sq_va, 0, (n + 1) * 32);
    memset(cq_va, 0, (n + 1) * 32);

    /* 6. 写 MCA 队列 0 的基址/偏移/属性
     *    SQDAO = 0x21000, SQISAO = 0x21040
     *    CQDAO = 0x21080, CQISAO = 0x210C0
     */
    W32(MCA_Q(0) + 0x04, (uint32_t)sq_dma);
    W32(MCA_Q(0) + 0x08, (uint32_t)(sq_dma >> 32));
    W32(MCA_Q(0) + 0x0C, 0x00021000u);
    W32(MCA_Q(0) + 0x10, 0x00021040u);

    W32(MCA_Q(0) + 0x24, (uint32_t)cq_dma);
    W32(MCA_Q(0) + 0x28, (uint32_t)(cq_dma >> 32));
    W32(MCA_Q(0) + 0x2C, 0x00021080u);
    W32(MCA_Q(0) + 0x30, 0x000210C0u);

    /* 先 CQ 后 SQ：enable 位最后置 */
    W32(MCA_Q(0) + 0x20, 0x80000000u | qsize);
    W32(MCA_Q(0) + 0x00, 0x80000000u | qsize);   /* i=0 -> CQ id 0 */

    /* 7. MAC = n-1，保留其它位 */
    {
        uint32_t v = R32(UFS_MMIO + R_MCQMAC);
        v = (v & ~0x0001FF00u) | (((n - 1) & 0x1FF) << 8);
        W32(UFS_MMIO + R_MCQMAC, v);
    }

    /* 8. 可选 ESI：
     *    W32(UFS_MMIO+0x384, msi_addr_lo);
     *    W32(UFS_MMIO+0x388, msi_addr_hi);
     *    W32(UFS_MMIO+0x271C, (R32(...) & ~0x007FF000u) | 0x0001F000u);
     *    W32(UFS_MMIO+R_MCQCFG, R32(...) | 2);
     */

    return 0;
}

/* 提交一个 SQE 到槽 0；ucd 里已经填好命令 UPIU/PRDT */
void mcq_submit_slot0(void *sq_va, uint64_t ucd_dma, int is_read, int prdt_len)
{
    uint32_t *sqe = (uint32_t *)sq_va;

    memset(sqe, 0, 32);

    /* DW0: byte3 = command_type(0) | data_direction | interrupt
     * 读=1，写=2，放在 bits2:1；bit0=1 表示完成中断（MCQ 可置 0）
     */
    sqe[0] = (is_read ? (1u << 9) : (2u << 9));   /* byte3 bits1:2 */

    /* DW2: OCS 初值 0xF（byte8） */
    sqe[2] = 0x0000000Fu;

    /* DW4-5: UCD DMA */
    sqe[4] = (uint32_t)ucd_dma;
    sqe[5] = (uint32_t)(ucd_dma >> 32);

    /* DW6: response length = 0x200, offset = 0x200 */
    sqe[6] = (0x200u << 16) | 0x200u;

    /* DW7: PRDT length, offset = 0x400 */
    sqe[7] = (0x400u << 16) | (uint32_t)prdt_len;

    /* 清 cache / DSB，确保 SQE 对设备可见 */
    cache_clean_to_poc(sqe, 32);
    dsb_sy();

    /* SQTP = ((0+1) % max_entries) << 5 = 0x20 */
    W32(SQD_Q(0) + 0x04, 0x20);
}

/* 轮询一个完成；返回 OCS，response UPIU 在 ucd_va+0x200 */
int mcq_poll_one(void *cq_va, void *ucd_va, uint32_t max_entries)
{
    uint32_t head = 0;

    (void)ucd_va;   /* response UPIU 由调用者按 UCD 地址读取 */

    for (;;) {
        uint32_t tail = R32(CQD_Q(0) + 0x04) >> 5;  /* CQTP */

        if (tail == head)
            continue;

        while (head != tail) {
            uint32_t *cqe = (uint32_t *)((char *)cq_va + head * 32);

            if (cqe[0] != 0) {                       /* DW0 有效 */
                uint64_t ucd = (cqe[0] | ((uint64_t)cqe[1] << 32)) & ~0x7FULL;
                uint32_t ocs = cqe[4] & 0xF;         /* status[3:0] */

                cqe[0] = 0;                          /* 清空 */
                cqe[1] = 0;

                head = (head + 1) % max_entries;
                /* CQHP = 下一个待处理槽的字节偏移 */
                W32(CQD_Q(0) + 0x00, head << 5);

                /* response UPIU 在 (ucd - ucd_dma) + ucd_va + 0x200 */
                (void)ucd;
                return (int)ocs;
            }

            head = (head + 1) % max_entries;
        }
    }
}
```

**照抄时的三个硬约束**

1. `sq_dma`/`cq_dma`/`ucd_dma` 必须是设备可访问的 DMA 地址（IOVA/PA），
   且 `ucd_dma` 低 7 位为 0。
2. `CQE.DW0` 是“有效”标志，初始化必须清零；处理完必须回写 0。
3. 门铃/head/tail 都是**字节偏移**（`槽号 << 5`），不是槽号。

---

## 9. Legacy 门铃路径

### 9.1 代码路径确实存在

`ufshcd_send_command` 的 legacy 分支（`0xffffffc080ce30a8`）：

```
0xce30a8  add  x0,x19,#0x78        ; hba->host_lock?
0xce30ac  bl   _raw_spin_lock_irqsave
0xce30b0  ldr  x8,[x19,#0xa8]      ; vops
0xce30bc  ldr  x8,[x8,#0x58]       ; setup_xfer_req
0xce30f0  blr  x8
0xce30f4  ldrsw x8,[x20,#0x50]     ; tag
0xce30f8  mov  w10,#1
0xce30fc  lsr  x9,x8,#6
0xce3100  lsl  x8,x10,x8           ; 1<<tag
0xce3104  add  x9,x19,x9,lsl #3
0xce3108  ldr  x11,[x9,#0x80]      ; 软件 tag 位图
0xce310c  orr  x8,x11,x8
0xce3110  str  x8,[x9,#0x80]
0xce3114  ldr  w8,[x20,#0x50]
0xce3118  lsl  w0,w10,w8           ; 1<<tag
0xce311c  ldr  x8,[x19]            ; mmio
0xce3120  add  x1,x8,#0x58         ; UTRLDBR
0xce3124  bl   writel
```

即：**legacy 门铃是 `MMIO+0x58`，写 `1<<tag`**。

### 9.2 但 `use_mcq_mode=0` 可能被拒绝

`ufshcd_init` 里的判定：

```
0xcde6d4  adrp x8, use_mcq_mode
0xcde6d8  ldrb w8,[x8,#0xd60]
0xcde6dc  tbnz w8,#0, 0xcde704      ; use_mcq_mode=1 -> MCQ 路径
0xcde6e0  ldrb w8,[x25,#0x31c]      ; lsdb_sup
0xcde6e8  tbz  w8,#0, 0xcdeb48      ; lsdb_sup=0 -> 报错
...
0xcdeb48  adrp x8, 0xffffffc0817d4000
0xcdeb4c  add  x8,x8,#0x474         ; "%s: failed to initialize (legacy doorbell mode not supported)"
0xcdeb60  bl   _dev_err
0xcdeb64  mov  w23,#-0x16           ; -EINVAL
```

`lsdb_sup` 的来源：

```
0xcde158  ubfx w11,w8,#25,#1        ; quirks bit25 (BROKEN_LSDBS_CAP)
0xcde164  tst  w9,#0x20000000       ; CAP bit29
0xcde168  csinc w9,w11,wzr,ne       ; CAP[29] ? quirk25 : 1
0xcde170  strb w9,[x25,#0x31c]
```

等价于：

```
lsdb_sup = !(CAP & BIT(29)) || (quirks & BIT(25));
```

所以：

- 如果本机 `CAP[29] == 0`（或带 `BROKEN_LSDBS_CAP` quirk），
  `use_mcq_mode=0` 可以通过，legacy 路径可用。
- 如果 `CAP[29] == 1` 且没有该 quirk，**显式 `use_mcq_mode=0` 会在 probe 时直接
  `-EINVAL`**，不会走到 legacy 门铃。
- 本机 CAP 的 bit29 静态未知；厂商默认 `use_mcq_mode=1`，没有实测过 legacy 分支。

### 9.3 唯一被代码“祝福”的运行时切换

`ufshcd_device_init` 的 MCQ 初始化失败分支：

```
0xce5710  mov  w21,w0
0xce5714  mov  x0,x19
0xce5718  bl   ufshcd_mcq_disable       ; 0x300 bit0=0
0xce571c  ...
0xce5724  add  x1,x1,#0x708             ; "MCQ mode is disabled, err=%d"
0xce572c  strb wzr,[x22,#0xd60]         ; use_mcq_mode = 0
0xce5730  bl   _dev_err
0xce5734  b    0xce51a4                 ; scsi_add_host，继续 legacy
```

`ufshcd_mcq_disable`：

```
0xcf27fc  ldr  x8,[x0]
0xcf2804  add  x0,x8,#0x300
0xcf2808  bl   readl
0xcf2810  and  w0,w0,#0xfffffffe
0xcf2818  bl   writel
0xcf2820  strb wzr,[x19,#0x131d]        ; mcq_enabled=0
```

**结论**：

- 代码保留了 legacy 门铃路径（`0x58`、`1<<tag`、`setup_xfer_req`）。
- 想在 Linux 里显式选 legacy：
  1. 首选让 MCQ 初始化失败后自动回退（不可控，不推荐）；
  2. 或 `echo 0 > /sys/module/ufshcd_core/parameters/use_mcq_mode` 后重绑，
     **前提是 `lsdb_sup=1`**，否则 probe 报
     “legacy doorbell mode not supported”；
  3. 运行中切换：`ufshcd_mcq_disable()`（清 `0x300 bit0`）→ 重新装 UTRLBA
     （`0x50/0x54`）→ 写 `0x58`。不要既开 MCQ 队列又敲 `0x58`。
- 对独立 UEFI 驱动：只要硬件实现了 legacy 门铃，可以直接清 `0x300 bit0`、
  配 UTRLBA、写 `0x58`；但本机是否实现，静态分析无法证明，**需要实测**。

---

## 10. 六个问题的直接回答

### Q1. MCA / MCQ 配置区

- **MCA 基址**：`MMIO + (MCQCAP[23:16] << 9)`；本机 `0x1d84000 + 0x1C000 = 0x1DA0000`。
- **每队列**：`MCA + 0x40*i`；`SQATTR/CQATTR` 的 `bit31` 使能，
  `[15:0]` 是 `max_entries*8-1`；`[20:16]` 是 CQ id（仅 SQATTR）。
- **SQ/CQ 基址**：`SQLBA/SQUBA`（`0x04/0x08`）、`CQLBA/CQUBA`（`0x24/0x28`）。
- **深度**：`max_entries = nutrs + 1`，`nutrs = min(64, bQueueDepth)`；
  `qsize = max_entries*8 - 1`。
- **门铃偏移**：`SQDAO`（`0x0C`）、`CQDAO`（`0x2C`），单位是相对 MMIO 的字节偏移。
- **`MCQCFG 0x300`**：`bit0` 总开关，`bit1` ESI。`0x380` 才是 MAC。
- **证据**：`0xcde17c`/`0xcde18c`（读 MCQCAP）；`0xcf20f4`/`0xcf20f8`（QCFGPTR）；
  `0xcf2584`–`0xcf2638`（写 MCA）；`0xcf1fa0`–`0xcf1fb8`（MAC）。

### Q2. SQ 门铃

- **不是** `0x58`，也**不是** MCA 配置区，而是 `mcq_sqd` 内存映射区里的
  **每队列寄存器**：`0x1DA5000 + i*0x100 + 0x04`（SQTP）。
- 写入值：`((tail + 1) % max_entries) << 5`；`tail` 是队列内槽号。
- 例：槽 0、`max_entries=33` → 写 `0x20`。
- 证据：`0xce3080 ldr x1,[x21,#0x8]`；`0xce3084 add w10,w8,#1`；
  `0xce3088 cmp w10,w9`；`0xce308c csinc w8,wzr,w8,eq`；
  `0xce3090 lsl w0,w8,#5`；`0xce3098 bl writel`。

### Q3. SQE 格式

- 32 B，和 legacy UTRD **逐字节相同**；`ufshcd_send_command` 直接 32 B 复制。
- `+0x10` = UCD DMA；`+0x18/0x1A` = response length/offset（0x200）；
  `+0x1C/0x1E` = PRDT length/offset（0x400）；`byte3 bit0` = interrupt，
  `bits2:1` = direction，`bits7:4` = command_type。
- 与 legacy 的差异不在描述符，而在**放置/索引/门铃/完成**。
- 证据：`0xce3050`–`0xce3074`（32 B 复制）；`0xce5da4`（UCD 指针在 +0x10）；
  `0xcf302c`–`0xcf303c`（command_type=0xF）。

### Q4. CQ 格式与完成轮询

- 32 B/CQE；`DW0` 非 0 = 有效；`DW4[3:0]` = OCS。
- 轮询：读 `CQTP`（`CQD+0x04`）`>>5` 得 tail，比较 `head`；
  逐项检查 `DW0`，调用完成处理，清 `DW0`，写 `CQHP = 新 head<<5`。
- response UPIU 不在 CQE 里，在 UCD+0x200。
- 证据：`0xcf2400`–`0xcf24a4`；`0xcda4e4`–`0xcda524`。

### Q5. 使能顺序

1. 读 CAP/MCQCAP，算 MCA。
2. HCE=1（`0x34`）。
3. `make_hba_operational`：IE、UTRL/UTMRL、HCS ready、run 位。
4. `0x300 bit0=1`。
5. `ufshcd_mcq_init`：映射 `mcq`/`mcq_sqd`，分配 `uhq`，`max_entries=nutrs+1`。
6. `ufshcd_mcq_memory_alloc`：每队列 SQ/CQ DMA。
7. `ufshcd_config_mcq`：可选 ESI → IE → MCA 队列 → MAC。
8. 可选 ESI：MSI、`0x384/0x388`、`0x271C`、`0x300 bit1`。
9. 第一条命令：构造 SQE → 写 SQTP。

**证据**：`0xce5188`–`0xce51a4`（enable → alloc → config）；`0xcd9cf0`
（make_hba_operational）；`0xcf2a04`（max_entries）；`0xcf24dc`（写队列）。

**ESI 非必需**；不配也能轮询完成。

### Q6. Legacy 路径

- 有代码路径：`0x58 = 1<<tag`，并在敲铃前调用 `vops->setup_xfer_req`。
- 但 `use_mcq_mode=0` 只有在 `lsdb_sup` 为真时才被允许；
  否则 probe 打印 “failed to initialize (legacy doorbell mode not supported)”
  并返回 `-EINVAL`。
- 运行时切换的官方路径是 `ufshcd_mcq_disable`（清 `0x300 bit0`），
  这也是 MCQ 初始化失败后的自动回退路径。
- 不要同时保留 MCQ 队列使能和 `0x58` 门铃。
- **证据**：`0xce30a8`–`0xce3124`（legacy 分支）；`0xcde6d4`–`0xcde6e8`（lsdb 判定）；
  `0xce5718`–`0xce572c`（MCQ 失败回退）。

---

## 11. 未验证项 / 风险

1. **`nutrs` 具体值**：静态只能确定 `min(64, bQueueDepth)`；本机未读设备描述符，
   常见为 32。写配方时用 `N` 参数化，不要硬编码 32。
2. **CAP[29] / legacy 是否真的可用**：静态未知；字符串和分支只能证明
   “某些配置下会被拒绝”。真机 legacy 需要实测。
3. **`0x04C` 厂商寄存器**：`make_hba_operational` 会写，但语义未公开；
   最小实现可以先跳过（若 HCS 已 ready）。
4. **缓存一致性**：本配方默认 SQ/CQ/UCD 是 uncached 或已 clean；
   ARM64 上必须在写门铃前 clean，读 CQE 前 invalidate。
5. **队列数**：本机 9 队列的推断来自 MCA 窗口 `0x240` 和 8 条 ESI；
   若只用 1 个队列，MCA 只需 `0x40`，SQD 块 0 足够。
6. **模块版本**：`ufs-qcom.ko` vermagic 为 6.12.23，vmlinux 为 6.12.38；
   结构偏移已交叉核对，但若替换 stock 镜像，需重新确认。

---

## 附录 A. 关键证据摘录（地址 + 指令）

### A.1 MCA / 深度

```
ffffffc080cf20f4: 39425808  ldrb  w8,[x0,#0x96]        ; QCFGPTR
ffffffc080cf20f8: 53175900  lsl   w0,w8,#9             ; *0x200
ffffffc080cf4fd8: f9400014  ldr   x20,[x0]             ; mmio
ffffffc080cf4fe0: 97fff445  bl    ufshcd_mcq_queue_cfg_addr
ffffffc080cf4fe4: 8b204288  add   x8,x20,w0,uxtw
ffffffc080cf4fec: f909e668  str   x8,[x19,#0x13c8]     ; mcq_base
```

### A.2 写 MCA 队列

```
ffffffc080cf2520: 0b140f28  add   w8,w25,w20,lsl #3    ; 0xffff + max_entries*8
ffffffc080cf2530: 33003d14  bfxil w20,w8,#0,#16
ffffffc080cf2528: 52b00014  mov   w20,#0x80000000
ffffffc080cf2544: 2a150280  orr   w0,w20,w21           ; | (i<<16)
ffffffc080cf2588: 8b090101  add   x1,x8,x9             ; +0x04 ...
ffffffc080cf258c: 94000054  bl    writel_relaxed
...
ffffffc080cf26b0: 91001101  add   x1,x8,#0x4           ; CQIE
ffffffc080cf26b4: 97fffe66  bl    writel
```

### A.3 门铃 / SQE 复制

```
ffffffc080ce3044: 394c76c8  ldrb  w8,[x22,#0x31d]      ; mcq_enabled
ffffffc080ce3048: 7100051f  cmp   w8,#1
ffffffc080ce304c: 540002e1  b.ne  0xffffffc080ce30a8   ; legacy
ffffffc080ce3050: f9400293  ldr   x19,[x20]            ; utrd
ffffffc080ce305c: f94012a8  ldr   x8,[x21,#0x20]       ; sqe_base
ffffffc080ce3060: b9404aa9  ldr   w9,[x21,#0x48]       ; tail
ffffffc080ce3068: 8b091508  add   x8,x8,x9,lsl #5
ffffffc080ce3070: a900310b  stp   x11,x12,[x8]
ffffffc080ce3074: a9012909  stp   x9,x10,[x8,#0x10]
ffffffc080ce3084: 1100050a  add   w10,w8,#1
ffffffc080ce3088: 6b09015f  cmp   w10,w9
ffffffc080ce308c: 1a8807e8  csinc w8,wzr,w8,eq
ffffffc080ce3090: 531b6900  lsl   w0,w8,#5
ffffffc080ce3098: 97ffdb5d  bl    writel
```

### A.4 CQ 轮询

```
ffffffc080cf2400: f9400e68  ldr   x8,[x19,#0x18]       ; CQTP
ffffffc080cf240c: 97fffef0  bl    readl
ffffffc080cf2414: 53057c08  lsr   w8,w0,#5
ffffffc080cf2458: f94002ca  ldr   x10,[x22]
ffffffc080cf245c: b4fffe8a  cbz   x10,...
ffffffc080cf2468: 9279e14a  and   x10,x10,#~0x7f
ffffffc080cf2484: 97ff9fea  bl    ufshcd_compl_one_cqe
ffffffc080cf2488: f90002df  str   xzr,[x22]
ffffffc080cf2498: 531b6900  lsl   w0,w8,#5
ffffffc080cf24a4: 97fffeea  bl    writel
```

### A.5 MAC

```
ffffffc080cf1f98: 910e0100  add   x0,x8,#0x380
ffffffc080cf1fa0: 321823e8  mov   w8,#0x1ff00
ffffffc080cf1fa4: 0b142108  add   w8,w8,w20,lsl #8
ffffffc080cf1fa8: 53087d08  lsr   w8,w8,#8
ffffffc080cf1fac: 33182100  bfi   w0,w8,#8,#9
ffffffc080cf1fb8: 94000025  bl    writel
```

### A.6 Legacy

```
ffffffc080ce3118: 1ac82140  lsl   w0,w10,w8           ; 1<<tag
ffffffc080ce3120: 91016101  add   x1,x8,#0x58         ; UTRLDBR
ffffffc080ce3124: 97ffdb3a  bl    writel
```

### A.7 字符串与地址

| 字符串 | VA | 用途 |
|---|---|---|
| `%s: failed to initialize (legacy doorbell mode not supported)` | `0xffffffc0817d4474` | `0xcdeb48` |
| `MCQ mode is disabled, err=%d` | `0xffffffc08173b708` | `0xce5724` |
| `MCQ configured, nr_queues=%d, ...` | `0xffffffc0816e620e` | `0xce5ae0` |
| `ESI %sconfigured` | `0xffffffc081730ae2` | `0xce5a70` |

---

## 附录 B. 最短检查清单（给实现者）

- [ ] 读 `0x004`，确认 `QCFGPTR`，算 `MCA`。
- [ ] 清 `0x300 bit0`，确保队列停。
- [ ] 分配 SQ/CQ（`(N+1)*32`，4 KB 对齐更稳）和 UCD（`0x400+`，128 B 对齐）。
- [ ] 写 MCA q0 的 `SQLBA/SQUBA/SQDAO/SQISAO/CQLBA/CQUBA/CQDAO/CQISAO`。
- [ ] 写 `CQATTR`、`SQATTR`（`0x80000000 | (N+1)*8-1`）。
- [ ] 写 `0x380 = (old & ~0x1FF00) | ((N-1)<<8)`。
- [ ] 写 `0x300 |= 1`。
- [ ] SQE 槽 0：`+0x10=UCD`，`+0x18/1A=0x200`，`+0x1C/1E=PRDT`。
- [ ] clean cache；写 `SQD+4 = 0x20`。
- [ ] 轮询 `CQD+4 >> 5`，读 CQE，取 `status & 0xF`，清 DW0，写 `CQHP`。
- [ ] 读 `UCD+0x200` 的 response UPIU。
- [ ] 若要中断：配 ESI 或读 `mcq_vs+0x8` 的 CQIS_VS。

---

*本文所有地址均为 vmlinux 链接地址（基址 `0xffffffc080000000`）或
`ufs-qcom.ko` 的 `.text` 段内偏移；指令字节与反汇编一致。*
