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
| 存储布局 | `SAMSUNG KLUGG8NHKB-F0H1`（FW 0500），`0xED4F800` 块 × 4096 B ≈ 1 TB，LUN 掩码 `0x3f`；GPT/根卷窗口待分区步骤 | 已记录 |

## 与 ShengUfs（SM8550 常量）的差异

**已从实机 vendor_boot ramdisk 取出 `gcc-canoe.ko`（133,496 B）并用其符号表读出每个 UFS clock 的
`hwcg_reg`/`halt_reg`**：

| 时钟 | SM8550 `GccUfs.h` | canoe 实测 |
|---|---|---|
| `gcc_ufs_phy_gdscr` | `0x77004` | `0x77004` |
| `gcc_ufs_mem_phy_gdscr` | `0x9e000` | `0x9e000` |
| `gcc_ufs_phy_axi_clk` | `0x77018` | `0x77018` |
| `gcc_ufs_phy_ahb_clk` | `0x77024` | `0x77028` |
| `gcc_ufs_phy_unipro_core_clk` | `0x77068` | `0x7706c` |
| `gcc_ufs_phy_ice_core_clk` | `0x77074` | `0x7707c` |
| `gcc_ufs_phy_phy_aux_clk` | `0x770b0` | `0x770bc` |
| `gcc_ufs_phy_rx_symbol_0_clk` | `0x7702c` | `0x77030` |
| `gcc_ufs_phy_rx_symbol_1_clk` | `0x770cc` | `0x770d8` |
| `gcc_ufs_phy_tx_symbol_0_clk` | `0x77028` | `0x7702c` |
| `gcc_aggre_ufs_phy_axi_clk` | `0x770e4` | `0x770f0` |

结论：**GDSCR 与 AXI 分支相同，其余分支全部偏移不同**（AHB/UNIPRO/ICE/PHY_AUX/RX_SYM0/RX_SYM1/TX_SYM0/AGGRE）。
`ShengUfs` 现有 `GccUfs.h` 不能直接用于 nx741j，需按上表覆盖。

vendor_boot ramdisk 同时提供 `phy-qcom-ufs-qmp-v4-canoe.ko`、`ufs-qcom.ko`、`ufshcd-crypto-qti.ko`，
可用于核对 PHY 初始化与 ICE 路径。

## 结论（本阶段）

- **硬件清单已完成**（控制器/PHY/GCC/SMMU/stream/中断/电源/ICE/MCQ）；
- **canoe GCC UFS 偏移已取得**（上表），可用 `local.toml` 覆盖 `ShengUfs` 的 `GccUfs.h` 后选中；
  PHY 序列仍需按 `phy-qcom-ufs-qmp-v4-canoe.ko` 核对。按官方要求先做适配：
  1. 取得 canoe GCC 的 UFS 分支/复位偏移（从 stock 内核 `gcc-canoe` 时钟驱动或运行时读取）；
  2. 用 `local.toml` 覆盖 `ShengUfs` 源码，替换 `GccUfs.h` 常量并补 MCQ/ICE 分支；
  3. 在 `board.toml` 选中 `ShengUfs`、`[kernel] archives` 加 `component:ShengUfs`、defconfig 开 `CONFIG_XNU_NATIVE_UFS=y`，内核参数加 `native-ufs-root=1`；
  4. 从本机 GPT 填 `[userdata]`，按所选 APFS 卷填 `root_match_volume_uuid`；
- 设备侧验证按官方完成条件：寄存器访问 → DMA/命令 → 已知块读取 → 根卷挂载 → 持久化。当前层 4 未通，验证排在其后。

## 证据

- `dtc -I dtb -O dts stock/dt/live_fdt.dtb`（91 万字节，含 `ufshc@1d84000`、`ufsphy_mem@1d80000`、`apps-smmu@15000000`）；
- 与 ShengUfs `GccUfs.h`（SM8550 常量）逐项对比见上表；
- 设备仓库 revision `19bf93e87bfa0e3d61c4790dbaf6456c184bb7f7`。

## 适配进展（ShengUfs 选中尝试）

已完成的准备：

- 从本机 `gcc-canoe.ko` 取得 canoe 分支偏移并生成 `ShengUfs-canoe` 本地覆盖（`GccUfs.h` 已改）；
- `local.toml` 增加 `[[source]] kind="driver" name="ShengUfs" local="/opt/work/work-local/ShengUfs-canoe"`；
- 曾临时选中 `ShengUfs` + `VirtioMmioBlock`（组件/archives/defconfig/kernel_args），`status` 解析通过。

内核链接失败（缺设备仓库应当提供的 ABI 归档）：

```
__ZTV20IOBlockStorageDevice        (IOBlockStorageDevice vtable)
_boot_progress_poll
_vr_open / _vr_verify_block / _vr_xnu_crypto / _verified_root_key / _verified_root_policies / _verified_root_policy_count
```

对照官方 sheng 参考 `board.toml`，这些符号由设备仓库脚本提供：
`build-sheng-source-storage.py`、`build-boot-progress.py`、`build-virtio-mmio-bindings.py`（RamBlock/VirtioMmioBlock）。
nx741j 还需为 iOS 16 profile 提供对应策略目录（`scripts/policies/ios-16.7.16-20H392`）。

**当前状态**：已回退组件选中（保持内核可构建），源码覆盖与记录保留。

### 私有仓库检查结果（UniXNU-sheng.zip）

| 内容 | 结果 |
|---|---|
| `package/BootProgress.c`、`BootFaultProbe.c`、`BootWatchdogPolicy.c`、`BootDiagExport.c`、`BootLatencyTrace.c`、`AdspFirmware.c` | ✅ 在 `package/`（不是脚本期望的 `kits/sheng/`），可复制后编译 |
| `scripts/build-boot-progress.py` | ✅ 存在，但硬编码读取 `kits/sheng/*.c` |
| `scripts/build-sheng-source-storage.py` | ❌ **zip 内没有**（公开仓库 404） |
| `scripts/policies/` | 只有 `ipados-26.5.2-23F84`、`ipados-27.0.1-24A446`，**没有 iOS 16** |
| `kits/sheng/` | ❌ 空（`kits` 为 submodule，zip 未包含） |
| `patches/xnu-12377/source-storage-runtime.patch` | ✅ 存在，但目标为 xnu-12377，**无 xnu-8796 版本** |

### 通用 ABI 路径尝试

`scripts/audit-storage-interface.py` 期望 Apple 公开 Darwin 23 源码：

- 已克隆 `apple-oss-distributions/IOStorageFamily` 并切到脚本要求的
  `e45fbe30a7879e0b16e26dddb78538f1b00e0bff`；
- 在组装树执行 `python3 scripts/audit-storage-interface.py`，参考对象
  `IOBlockStorageDevice.cpp` 编译失败（15 处错误，`KALLOC_TYPE_DEFINE` 等宏与 xnu-8796 头文件不兼容）。

结论：通用审计路径要求 **Darwin 23** 的 IOStorageFamily 与对应 XNU 头文件；本机是 xnu-8796
（iOS 16 / Darwin 21），版本不匹配。

### 解除阻塞所需输入（三选一）

1. 官方 **iOS 16 的 storage policy 归档脚本**（`build-sheng-source-storage.py`）与
   `scripts/policies/ios-16.7.16-20H392`；
2. `source-storage-runtime.patch` 的 **xnu-8796 版本**（当前只有 xnu-12377）；
3. 与 xnu-8796 匹配的公开 IOStorageFamily 源码版本（用于重新生成 `storage-abi` 审计与绑定）。

## 构建已打通（本地 bring-up）

`ShengUfs` 已选中并链接成功（`kernel.macho` sha256 `ec801093…`）：官方 audit 产出
`IOBlockStorageDevice-reference.o`，设备仓库 `package/storage-bringup/Stubs.c` +
`scripts/build-storage-bringup.py` 提供剩余符号，框架 `build-xnu-4k-baseline.sh` 追加
`-force_load` 入链接。运行时仍受 `native-source-storage=1` 门控，stub 绕过 verified-root，
正式启用前需替换为私有 storage policy 归档。详见 `docs/STORAGE-BRINGUP.md`。
