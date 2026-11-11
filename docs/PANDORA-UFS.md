# 参考 data-a-live（pandora，SM8850）的 UFS 驱动分析

对象：`repos/data-a-live`（小米 17 Pro，SM8850，实机进过 iOS 锁屏/主屏）与 `repos/ios-sweet`。
证据：`xnu/patches/0028-sweet-ufs-root-device.patch`（完整驱动）、`wsl/pandora_ufs_*.c.txt`（探针/diag）。

## 1. 它是怎么接管控制器的（`ufs_take_over_host`）

```c
hce = rd(0x34); hcs = rd(0x30);
if (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) return ENXIO;   // 必须 DP+UTRLRDY
if (rd(0x58) != 0) return EBUSY;                                          // 固件无未完成请求
wr(0x24, 0);            // IE=0，全程轮询
wr(0x20, rd(0x20));     // 清 IS
wr(0x60, 0);            // UTRLRSR=0
wr(0x50, list); wr(0x54, list>>32);   // 自己的 UTRL（1 KiB 对齐）
wr(0x60, 1);            // UTRLRSR=1
require rd(0x50)==list && rd(0x54)==hi && (rd(0x60)&1);   // 回读校验
```

要点：**完全不碰 UTMRL、不写 SMMU、不碰 MCQ**；列表来自 `kmem_alloc_contig` 的连续内核内存。

## 2. 它怎么发命令（`ufs_scsi`）

| UTRD 字段 | 值 |
|---|---|
| `dw0` | `(1<<28) \| (dir?2<<25:0) \| (1<<24)` |
| `dw2` | `0x0f`（OCS invalid） |
| `resp` | `(512/4)<<16 \| (512/4)`（响应 UPIU 偏移/长度，DWORD） |
| `prdt` | `(1024/4)<<16 \| n`；PRDT 项 256 KiB 一块，`dbc=chunk-1` |
| 门铃 | `wr(0x58, 1)`，然后轮询 `rd(0x58)&1`（5 s 超时），超时写 `0x5c` |
| 判定 | `ocs==0`、`resp[0]==0x21`（COMMAND UPIU）、`resp[6..7]==0` |

## 3. 与我们 NativeUfs 的差异（重点）

| 项 | pandora（可用） | 我们 | 影响 |
|---|---|---|---|
| 前置条件 | 要求 `HCE&1`、`HCS_DP`、`HCS_UTRLRDY`；`UTRLDBR==0` 才接管 | 未按位门控 | 条件不满足时硬装列表会清 ready 位 |
| UTMRL | **不碰** | 写 UTMRLBA/UTMRLRSR | 可能触发 HCS 0x10F→0x08（UTRLRDY/UTMRLRDY 同时清零） |
| SMMU | **不写**（沿用固件旁路） | 显式 bypass + 探测 | 多一步风险 |
| MCQ | **完全不涉及** | 试过 MCQ 路径也挂 | 见下节推断 |
| UTRD `dw0` | `bit28 \| dir<<25 \| bit24` | `0x10000001`（bit0 被置） | bit0 不是 UTRD 字段，可能让主机丢描述符 |
| DMA 内存 | `kmem_alloc_contig` 连续页 | 复用固件列表页 / 自分配 | 连续性与可映射性不同 |
| 命令重试 | 首条 TUR 重试 3 次（unit attention） | 单次 | 首次失败会被误判 |

## 4. 推断：本机固件把控制器留在 MCQ 模式

我们的实测：接管前 `hcs=0x10F`（DP/UTRLRDY/UTMRLRDY/UCRDY 全 1、UPMCRS=PWR_LOCAL），
写完 `UTRLBA/UTMRLBA + run-stop` 后 `hcs` 掉到 `0x08`（只剩 UCRDY）——**主机拒绝了我们装的列表**。
pandora 在同一 SoC 上用同样的 0x50/0x54/0x60 序列成功，差别只剩两条：

1. 它**不写 UTMRL**；
2. 它的机器上固件**没有把 MCQ 打开**，而我们 Android 侧实测 `use_mcq_mode=Y`、MCQ 窗口（0x1da0000）有独立 iomem 映射。

MCQ 模式下 legacy 队列寄存器可能被硬件忽略/清位——这与「写完 UTRLRSR 就掉 ready」吻合；
而门铃写 0x58 在 MCQ 开启时把总线挂死也解释得通。

## 5. 下一个实验（最小、可直接做）

在 `adoptAndRead()` 最前面加三步：

1. 读 `rd(mmio+0x300)`（MCQCFG）：若 bit0=1，写回 `val & ~1`（厂商 `ufshcd_mcq_disable` 同款）；
2. 重新读 `HCS`，要求 `DP|UTRLRDY` 仍为 1；
3. 按 pandora 序列：`IE=0 → 清 IS → UTRLRSR=0 → UTRLBA/U(自己的1KiB对齐连续页) → UTRLRSR=1 → 回读校验`，**不写 UTMRL**；
4. 然后发 TUR（重试 3 次）。

另：`dw0` 改成 `(1<<28)|(dir<<25)|(1<<24)`，去掉 bit0。

## 6. 顺带确认的寄存器

`0x4c` = **UTRIACR**（UFS Task Request Interrupt Aggregation Control），不是队列配置——
厂商 `ufshcd_make_hba_operational` 里对它写的就是聚合参数，pandora 的 diag 也把它和 `AHIT(0x18)` 一起读出来。
