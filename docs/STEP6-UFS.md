# 步骤 6 · UFS 存储（第 2 阶段）

官方[UFS 移植](https://unixnu.com/zh/porting/ufs)要求先完成硬件清单，再决定接管或冷初始化。
本页数据取自**本机实机 DTB**（`stock/dt/live_fdt.dtb`，SM8850 "canoe"），不是 SM8550 参考值。
设备仓库 revision `19bf93e87bfa0e3d61c4790dbaf6456c184bb7f7`。

## 硬件清单（实机 DTB）

| 输入 | 取值 | 与 SM8550 参考 |
|---|---|---|
| 控制器 | `ufshc@1d84000`，`reg`：`ufs_mem 0x1d84000+0x3000`、`ice 0x1d88000+0x18000`、`mcq_sqd 0x1da5000+0x2000`、`mcq_vs 0x1da4000+0x10` | 基址相同；**多了 MCQ 寄存器区** |
| compatible | `qcom,ufshc` | 相同 |
| 中断 | GIC SPI `0x109`（265），触发类型 `4`（level-high） | 需核对 SM8550 的 SPI 号 |
| PHY | `ufsphy_mem@1d80000`，`0x1d80000+0x2000`，`compatible = "qcom,ufs-phy-qmp-v4-canoe"` | 基址相同；**PHY 变体不同（canoe）** |
| GCC | `clock-controller@100000`，`compatible = "qcom,canoe-gcc"`，`reg 0x100000+0x1f4200` | 基址相同；**分支/复位偏移需按 canoe GCC 重算** |
| UFS 时钟 | `core_clk`(0x9b)、`bus_aggr_clk`(0x01)、`iface_clk`(0x9a)、`core_clk_unipro`(0xaa)、`core_clk_ice`(0x9e)、`ref_clk`(0x37/0x04)、`tx_lane0/rx_lane0/rx_lane1 sync`(0xa8/0xa4/0xa6) | ID 不同，需重映射 |
| 复位 | `resets = <0x45 0x19>`（GCC 0x19），`reset-names = "rst"` | ID 不同 |
| 电源 | `power-domains = <0x45 3>`；`vcc/vccq/vddp-ref-clk/vccq-parent` 供电 | 需在 XNU 侧接入或由固件保留 |
| SMMU | `apps-smmu@15000000`；`iommus = <0x23 0x60 0>` → **stream 0x60** | 与 SM8550 相同 stream |
| DMA | `dma-coherent`、`qcom,iommu-dma = "fastmap"`、`iommu-msi-size = 0x1000` | 需在接管时保留一致性 |
| ICE | `qcom,ice-use-hwkm`、`shared-ice-cfg`、`reg-names` 含 `ice` | SM8550 参考实现未覆盖 ICE |
| 通道/速率 | `lanes-per-direction = 2`，`qcom,prime-mask = 0xc0`、`silver-mask = 0x3f` | 影响 IRQ 亲和 |
| 存储布局 | 待从本机 GPT 读取（LUN/块大小/容量/根卷窗口） | 官方要求记录后再选 `[userdata]` |

## 与 ShengUfs（SM8550 常量）的差异

`ShengUfs` 的 `GccUfs.h` 把 SM8550 事实写死：

```
GCC_BASE 0x00100000, UFS_HC_BASE 0x01d84000, UFS_HC_SIZE 0x3000
GCC_UFS_PHY_GDSCR 0x77004, GCC_UFS_PHY_AXI_ENABLE 0x77018, ... GCC_AGGRE_UFS_PHY_AXI_ENABLE 0x770e4
```

实机确认 **控制器与 GCC 基址一致**，但：

1. GCC 是 `canoe-gcc`，UFS 分支/复位在寄存器表中的偏移需按 SM8850 时钟驱动重新推导（不能沿用 0x77004 等）；
2. PHY 为 `qmp-v4-canoe`，PHY 初始化/调优序列与 SM8550 不同；
3. 实机存在 **MCQ** 寄存器区（`mcq_sqd`/`mcq_vs`）与 ICE 配置，SM8550 参考实现只覆盖传统队列；
4. 时钟/复位 ID 与 SM8550 不同（见上表）。

## 结论（本阶段）

- **硬件清单已完成**（控制器/PHY/GCC/SMMU/stream/中断/电源/ICE/MCQ）；
- **暂不选中 `ShengUfs`**：沿用 SM8550 常量会导致 GCC 偏移与 PHY 序列错误。按官方要求先做适配：
  1. 取得 canoe GCC 的 UFS 分支/复位偏移（从 stock 内核 `gcc-canoe` 时钟驱动或运行时读取）；
  2. 用 `local.toml` 覆盖 `ShengUfs` 源码，替换 `GccUfs.h` 常量并补 MCQ/ICE 分支；
  3. 在 `board.toml` 选中 `ShengUfs`、`[kernel] archives` 加 `component:ShengUfs`、defconfig 开 `CONFIG_XNU_NATIVE_UFS=y`，内核参数加 `native-ufs-root=1`；
  4. 从本机 GPT 填 `[userdata]`，按所选 APFS 卷填 `root_match_volume_uuid`；
- 设备侧验证按官方完成条件：寄存器访问 → DMA/命令 → 已知块读取 → 根卷挂载 → 持久化。当前层 4 未通，验证排在其后。

## 证据

- `dtc -I dtb -O dts stock/dt/live_fdt.dtb`（91 万字节，含 `ufshc@1d84000`、`ufsphy_mem@1d80000`、`apps-smmu@15000000`）；
- 与 ShengUfs `GccUfs.h`（SM8550 常量）逐项对比见上表；
- 设备仓库 revision `19bf93e87bfa0e3d61c4790dbaf6456c184bb7f7`。
