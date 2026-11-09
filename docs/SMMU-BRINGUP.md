# ArmSmmu4K 接管 UFS stream 0x60：分步做法

目标：让 UFS 的 DMA 走我们建的 stage-1 表。五个前提条件按下面的顺序处理，每一步都有可观测的中间结果，
失败了能停在上一步、不碰硬件。

## 步骤 0：只读探测（先做这个）

`unixnu.local.toml` 加一个内核参数，然后重跑 payload + bootimg（bootargs 在 payload 里，不在 FD 里）：

```toml
kernel_args = ["-v", "serial=3", "smmu-probe=1"]
```

启动后 `board_smmu_probe()` 会打印（串口或 panic 前的屏幕）：

```
SMMU: scr0 002f0406 id0 08020xxx id1 … id2 … sacr 00000000 sfsr 00000000 slots 48
SMMU: slot 12 smr 80000060 s2cr 00000005 sid 060 mask 000 type 0 bank 5
SMMU: ufs sid 060 slot 12 bank 5 cbar 00010000
```

这一份 dump 同时回答了前四个问题里的三个：

| 看到什么 | 说明 |
|---|---|
| `id0` 是合理的 SMMUv2 值（非 `00000000`/`ffffffff`） | **时钟已经在跑**（原因 3：不需要接管 GCC，只要不动它） |
| `id0 = ffffffff` 或读不到 | 块没有时钟/没上电 → 必须先做 GCC 时钟，本路暂停 |
| `sid 060 … type 0 bank N` | **slot/bank 不用猜了**（原因 1 解决），attach 会直接用这个 slot/bank |
| `ufs sid 060 is not routed yet` | 固件没给 UFS 建路由 → 需要按 vendor 表挑 slot（此时才需要 vendor `arm-smmu.c` 的 SID 表） |
| `sfsr` 非 0 | 已经有挂起的全局故障，先读 `sfsr` 并清（`SFSR` 写 1 清），否则 attach 会被 kit 拒绝 |

`sacr` 是全局 ACTLR（原因 4 的一半）：和 DT 里 `qcom,actlr` 的取值比对；不一致先记下来，不要改。

## 步骤 1：把故障中断装上（原因 2）

kit 要求 attach 前有 fault handler，否则一次翻译错误就是总线挂死。DT 给了中断号：

- 全局故障 + 每 context bank 的故障：`apps-smmu@15000000` 的 `interrupts`，第一个是全局（SPI `0x41` = 65），其余约 100 条是 per-bank/per-stream；
- XNU 侧用 `ml_install_interrupt_handler()` 注册（`osfmk/arm/machine_routines.h`），handler 里只做两件事：
  1. 读 `SFSR`（`0x48`）并打印 `SMMU: global fault sfsr %08x`；
  2. 对每个 bank 读 `CBAR[bank]+0x58`（FSR）打印，然后清该 bank 的 FSR。

handler 里不要做翻译恢复（kit 明确说不合成恢复），只记录；把 SFSR 清掉即可避免中断风暴。

## 步骤 2：时钟/复位（原因 3）

不接管。步骤 0 的 `id0` 能读出来就说明 bootloader 留的时钟还在跑；XNU 侧没有 GCC 驱动，任何 reset 操作都可能把 UFS/SMMU 一起关掉。
因此：**只读、只写 SMMU 自己的寄存器，不碰 GCC**。等以后有 clock 驱动再谈所有权。

## 步骤 3：ACTLR/安全（原因 4）

`qcom,actlr` 是 `(sid, mask, actlr)` 三元组表，属于平台侧配置；标准 SMMUv2 窗口里只有全局 `sACR`（`0xC`）。

- 先只读：把步骤 0 的 `sacr` 和 DT 表里的默认值比对；
- per-SID ACTLR 的写入位置是 qcom 扩展寄存器，偏移只在 vendor `arm-smmu.c` 里，公开资料没有；
- 所以这一项先做成“记录 + 不改”，等拿到 vendor 偏移或实机对比再补。attach 用的是 `qcom,use-3-lvl-tables` 的常规路径，不依赖 per-SID ACTLR。

## 步骤 4：attach（原因 5 的实机迭代）

步骤 0 拿到 slot/bank、步骤 1 装好 handler、`sfsr` 清零之后：

```toml
kernel_args = ["-v", "serial=3", "smmu-probe=1", "smmu-attach=1"]
```

attach 做三件事（`board_smmu_attach_ufs()`）：

1. `ArmSmmu4KProgramDisabledContext(io, ufs_bank, ctx, 1000, &program)`；
2. `ArmSmmu4KAttachStreams(io, state, {slot, sid 0x60}, 1, ufs_bank, ctx, &result)`；
3. 打印 `SMMU: attach sid 060 slot … bank … -> 0`（0 = 成功）。

判读：

| 结果 | 含义 | 下一步 |
|---|---|---|
| `-> 0` 且 UFS 仍能读 | 接管成功 | 用 `ArmSmmu4KObserveOwnedStreams()` 定期校验，跑 disktool 压力 |
| `-> 4/5/6…`（非 0） | 状态不匹配/busy | 看打印的 `program bank … failed N`：先确认 bank 是 FAULT 状态、`sfsr` 已清 |
| 打印后立刻死机/复位 | 编程触发了总线错误 | 回退：把 `smmu-attach=1` 去掉，刷回上一版 boot.img |

## 回退与安全

- 每一步都有独立开关：`smmu-probe=1` 只读；`smmu-attach=1` 才写；两个都不加时只有纯内存建表；
- attach 失败时把 `attach_status` 留在 `board_smmu_status()` 里，panic 屏/串口能看到；
- 最坏情况用 9008 刷回上一版 `boot.img`（`z80u-collect/wsl/flash-boota-9008.ps1`），FD 不变。

## 现在已具备 / 还缺

| 项 | 状态 |
|---|---|
| 建表（含物理池、缓存发布） | ✅ 默认运行，kernel 链接已验证 |
| 只读探测 + SMR/S2CR dump + 自动发现 slot/bank | ✅ 本次实现，`smmu-probe=1` |
| 故障中断 handler | ⬜ 需要写 `ml_install_interrupt_handler` 的板级文件（步骤 1） |
| attach（编程 bank + 挂 stream） | ✅ 代码在，开关 `smmu-attach=1`，需要步骤 0/1 的数据 |
| per-SID ACTLR | ⬜ 需要 vendor 偏移 |
| 时钟/复位所有权 | ⏸ 不接管（只读验证块是否在跑） |
