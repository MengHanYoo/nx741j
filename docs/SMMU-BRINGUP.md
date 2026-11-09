# ArmSmmu4K 接线（UFS stream 0x60）

第 1 级的 `ArmSmmu4K` 是 kit（无归档），由存储归档编入。本页记录三个阶段、各自的开关、已验证的部分，
以及还不能接管真实 stream 的原因。

## 三个阶段

| 阶段 | 做什么 | 默认 | 开关 |
|---|---|---|---|
| build | 用 kit 的 `ArmSmmu4KBuild()` 在静态表池里生成 stage-1 表（4 KiB 叶），`FlushPoC_Dcache()` 发布 | **运行** | — |
| map/probe | `ml_io_map(0x15000000, 0x100000)` 映射寄存器，`ArmSmmu4KBindMmio()` 绑定，读 ID0/ID1/ID2/SCR0 | **不运行** | 内核参数 `smmu-probe=1` |
| attach | `ArmSmmu4KProgramDisabledContext(bank 0, asid 1)` + `ArmSmmu4KAttachStreams(slot 0x60, sid 0x60)` | **不运行** | 内核参数 `smmu-attach=1`（同时需要 probe） |

MMIO 默认不碰是有意的：SMMU 若未上电/未给时钟，第一次寄存器读就可能把总线挂死。表构建是纯内存操作，
所以默认执行——它同时验证了 kit 的参数约束（`input_bits 32`、`output_bits 40`、池 4 KiB 对齐、
range 4 KiB 对齐且不与表池重叠）。

## 表池

- `package/storage-bringup/SmmuBind.c` 里 `static uint64_t board_smmu_tables[8*512] __attribute__((aligned(4096)))`（32 KiB）；
- 物理地址用 `ml_vtophys()` 取，并逐页校验连续（不连续就报错退出，不会把错地址写给硬件）；
- 建表后用 `FlushPoC_Dcache()` + `__sync_synchronize()` 发布。

## 触发点

`NativeUfs.o` 里有一个 `U _boot_progress_poll`，存储驱动启动控制器时会调用它。绑定实现这个钩子：
首次调用执行 build（以及开关打开时的 map/probe/attach），结果存在 `board_smmu_status()` 里。

## 已验证（主机 + 构建）

| 项 | 结果 |
|---|---|
| 归档成员 | `ArmSmmu4K*.o`（8 个，kit 全部翻译单元）+ `SmmuBind.o` |
| kernel.macho | `_ArmSmmu4KBuild`、`_ArmSmmu4KBindMmio`、`_ArmSmmu4KEncodeContext`、`_ArmSmmu4KProgramDisabledContext`、`_ArmSmmu4KAttachStreams`、`_board_smmu_attach_ufs` 全部链接，sha256 `1f8216a6…` |
| `check-bringup.py` | 20/20（含"归档含 kit 与绑定""内核链接 `_ArmSmmu4KBuild`""`boot_progress_poll` 只有一处定义"） |

## 还不能接管真实 stream 的原因

1. **bank/slot 编号是假设**：`bank 0`、`slot = sid = 0x60` 是占位约定；qsmmu-v500 的 stream 映射表在 vendor
   内核（`qcom,smmu` 配置）里，DT 只给了 `qcom,actlr`，没有 SMR slot 布局。
2. **没有故障中断处理**：kit 要求 attach 前装好 SMMU 故障中断处理（`#global-interrupts` 那条 + 约 100 条 stream 中断）。
3. **没有时钟/复位所有权**：SMMU 由 GCC/dispcc 供时钟，XNU 侧还没有 clock 驱动，只能靠 bootloader 留下的状态。
4. **ACTLR/安全要求**：`qcom,actlr` 表要按平台写入，kit 明确要求调用方先建立平台 ACTLR/安全配置。
5. 以上都需要实机迭代，主机上只能验证到"编得进、链得上、参数检查通过"。

## 打开开关的方式

`unixnu.local.toml` 的 `kernel_args` 里加 `"smmu-probe=1"`（先只读探测），确认 ID 寄存器有合理值、机器不挂之后，
再加 `"smmu-attach=1"` 做真正的 context/stream 编程。
