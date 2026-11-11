# 选项 A：移植 ufs-qcom 的 PHY/时钟初始化

目标：不依赖 bootloader 留下的链路状态，自己把 UFS PHY 从冷状态带到 PCS ready，
再进 HCE/link startup。证据全部来自厂商模块与实机 DTS。

## 1. 证据来源

| 来源 | 内容 |
|---|---|
| `_tmp/t2/vr/lib/modules/phy-qcom-ufs-qmp-v4-canoe.ko` | canoe PHY 的 4 张标定表、`start_serdes`、`is_pcs_ready` |
| `_tmp/t2/vr/lib/modules/phy-qcom-ufs.ko` | 公共 `power_on`（vreg/时钟/复位）、`write_tbl` |
| `dts/nx741j-adt.dts:12786` | 实机 `ufshc` 的 9 个时钟、复位、OPP（100/403 MHz）、PHY 节点 |
| `docs/UFS-VENDOR-DIFF.md` | 厂商 HBA 初始化与两条门铃路径 |

## 2. 已还原的 PHY 寄存器序列

`ufs_qcom_phy_write_tbl(base, tbl, n)` 的条目是 **8 字节 `{u32 reg; u32 val;}`**，
写 `writel(val, base->mmio + reg)`；`calibrate` 依次写四张表：

| 表 | 条目 | 用途 |
|---|---|---|
| `phy_cal_table_rate_A_g5` | **106** | HS-G5 Rate A 全量 PCS/QSERDES 初始化 |
| `phy_cal_table_rate_A_g4` | 2 | G4 覆盖（0x0474/0x04bc） |
| `phy_cal_table_2nd_lane` | 1 | 第二 lane（0x05fc=2） |
| `phy_cal_table_rate_B` | 1 | Rate B（0x0420=0x41） |

`start_serdes`（canoe）：`writel(readl(mmio+0x400) | 1, mmio+0x400); dsb`；
`is_pcs_ready`：轮询 `mmio+0x400 & 1`（ktime 超时 + `usleep_range`）。

**推论**：PHY 就绪 = 表写完 + `0x400.bit0` 置位并回读为 1。

## 3. 时钟/电源（实机 DTS 给出完整清单）

| 名称 | provider | ID | 备注 |
|---|---|---|---|
| `core_clk` | gcc | 0x9b | OPP 100/403 MHz |
| `bus_aggr_clk` | gcc | 0x01 | 互联聚合 |
| `iface_clk` | gcc | 0x9a | AHB |
| `core_clk_unipro` | gcc | 0xaa | UniPro |
| `core_clk_ice` | gcc | 0x9e | ICE |
| `ref_clk` | **rpmh** | 0x04 | 19.2 MHz，来自 RPMh（非 GCC） |
| `tx_lane0_sync_clk` | gcc | 0xa8 | lane |
| `rx_lane0_sync_clk` | gcc | 0xa4 | lane |
| `rx_lane1_sync_clk` | gcc | 0xa6 | lane |
| `resets` | gcc | 0x19 | `rst` |
| `reset-gpios` | tlmm | 0xd9 | PHY 复位脚 |
| `power-domains` | gcc | 0x03 | UFS GDSC |

公共 `power_on` 顺序：**5 路 vreg（vcc/vccq/vddp-ref-clk/vccq-parent…）→ 9 个时钟 → 复位释放**，
没有任何 PHY 寄存器写；寄存器写全在 `calibrate`/`start_serdes`。

## 4. 移植到 ShengUfs 的调用顺序

```
ufs_qcom_phy_power_on()          // vreg/时钟/复位：接管时若固件已开则跳过
ufs_qcom_phy_qmp_v4_calibrate()  // 写 g5(106) + g4(2) + 2nd_lane(1) + rate_B(1)
ufs_qcom_phy_qmp_v4_start_serdes() // mmio[0x400] |= 1; dsb
ufs_qcom_phy_qmp_v4_is_pcs_ready() // 轮询 mmio[0x400]&1
ufs_hce_enable()                 // HCE=1, 等 HCS.UCRDY
ufs_link_startup()               // UIC DME_LINKSTARTUP (0x16) + 等 UPMCRS=PWR_LOCAL
```

与 `docs/PANDORA-UFS.md` 的接管路径互补：若 bootloader 的链路可用（HCS=0x10F），
可跳过前三步直接接管；只有冷启动/HCE reset 后才需要完整 PHY 初始化。

## 5. 产物

- `package/storage-bringup/ufs-qcom-phy.c`：四张表 + `write_tbl`/`calibrate`/`start_serdes`/`is_pcs_ready`，
  不依赖内核头（用 `volatile u32 *` 基址），可直接编进 ShengUfs 或做主机单测；
- 表值逐条来自 .data 反汇编，未做任何猜测。

## 6. 风险与待确认

1. `ref_clk` 来自 RPMh（不是 GCC），我们的内核没有 RPMh 驱动 → 若 ref_clk 被关，需要先确认固件是否已打开；
2. PHY 复位脚（tlmm 0xd9）与 GCC reset 0x19 的极性需实机确认；
3. 重写 PHY 时若固件仍在跑 UFS 命令，必须先确保控制器空闲（HCE=0、UTRLDBR=0）；
4. 表值 106 条里有 lane 相关项（0x05fc、0x0420 等），两 lane 配置与 `lanes-per-direction=2` 一致。

## 7. 接入 ShengUfs 与 QEMU 闸门（2026-11-12 01:0x）

### 接线方式

| 件 | 位置 | 作用 |
|---|---|---|
| `scripts/patch-native-ufs-cold-init.py` | 设备仓库 `scripts/` | 构建期在 `NativeUfs::start` 开头注入 `board_ufs_cold_init()` 调用（幂等，锚点缺失即报错） |
| `package/storage-bringup/ufs-cold-init.c` | 设备仓库 | 门控 `native-ufs-cold-init=1`；映射 PHY(0x1d80000)/HOST(0x1d84000)，按 calibrate → start_serdes → PCS ready → HCE → DME_LINKSTARTUP → UPMCRS 执行 |
| `package/storage-bringup/Stubs.c` | 同上 | `#include "ufs-cold-init.c"`，随现有 `Stubs.o` 归档成员进内核，**无需改归档脚本** |
| `board.toml` | 设备仓库 | `[kernel] archives` 在 `component:ShengUfs` 之前插入 patch 步骤；`kernel_args` 增加 `native-ufs-cold-init=1` |

### 构建结果

| 产物 | sha256 |
|---|---|
| `kernel.macho` | `5b650712d360926ab4bd3012103058802a142f61b80c9113cec2e6edc5c29c33` |
| `payload.img` | `53d93b6ba66eb2f4509e43945ddfa55e551f26866e4afe7430a8f392ea47a4d8` |
| `SILICIUM_UEFI.fd` | `c6520ada215d8e79d8259c7fb117d81fae08dc718879df925c15fe098931a2f1` |

`kernel.macho` 里已确认 `_board_ufs_cold_init` 存在；`NativeUfs.cpp:48` 有声明、`:2740` 有调用。

### QEMU 闸门

```
PASS  smoke: six package scenarios
PASS  UNIXNU: BootEntry
PASS  MARK #29
PASS  QEMU: EBS ok, entering the kernel
qemu gate passed: the payload reaches ExitBootServices and the kernel entry
```

五阶段未受影响（QEMU 不建模 UFS，冷启动路径默认不触发，且本次 QEMU 启动参数不含 `native-ufs-cold-init`）。

### 真机测试步骤（待执行）

1. efisp 已由用户刷好；
2. 9008 写 `payload.img` 到 `rawdump`+0（64 MiB）、`SILICIUM_UEFI.fd` 到 `rawdump`+16384 扇区；
3. 启动后观察屏幕 `UFS cold:` 四行（calibrating / PCS ready / link rc / link up）；
4. 若 `PCS not ready` → PHY 序列或 ref_clk 问题；若 `UPMCRS != 1` → 链路启动参数问题；
5. 冷启动路径成功后，再把 `native-ufs-probe` 调到 14 走接管读块。
