# 显示设备树：SDE / DSI / 面板（NX741J）

本页记录 MSM 显示控制器的设备树建模：节点从实机 stock FDT 提取，phandle 全部改成
label 引用，编入 `dts/nx741j.dts`。驱动本身（DPU/DSI/背光）仍未实现，这里只把
硬件描述、绑定和连线固定下来，见 `STEP6-DRIVERS-MATRIX.md` 第 3 级。

## 文件

| 文件 | 来源 | 说明 |
|---|---|---|
| `dts/nx741j-mdss.dtsi` | `scripts/mk-mdss-dtsi.py` 生成 | mdss(SDE) + dsi0/1 + dsi_phy0/1 + 连接器 + 面板供电 + 占位 provider，33 个实节点、19 个占位节点 |
| `dts/nx741j-panel-bf375.dtsi` | 手工整理自 live FDT | BOE BF375 + RM692H5 面板，挂在 `&dsi0` 下，4 组 timing（60/90/120/144 Hz） |
| `scripts/mk-mdss-dtsi.py` | — | 生成器：解析 live dts，按属性白名单改写 phandle，产出上面的 dtsi |

生成器输入是实机 `/sys/firmware/fdt`（本仓库外的 `_tmp/live-fdt.dtb`，反编译后 1.4 MB）：

```sh
dtc -I dtb -O dts -o live.dts live-fdt.dtb
python3 scripts/mk-mdss-dtsi.py --fdt live-fdt.dtb --live live.dts --out dts/nx741j-mdss.dtsi
```

## 拓扑

| 节点 | 地址 / 中断 | 说明 |
|---|---|---|
| `mdss: qcom,mdss_mdp@9800000` | `0x9800000+0x1c8800`（mdp）、`0x9b16000+0x2008`（vbif）、`0x9b60000+0x7000`（regdma）、`0x1100000+0x1000`（ipcc）、`0x9aa0000+0x13c`（swfuse）；IRQ 0x53 | SDE 顶层，202 个配置属性（pipe/mixer/DSC/DS++ 表）；`qcom,sde-hw-version = 0xd0000000` |
| `dsi0: qcom,mdss_dsi_ctrl0@9ac0000` | `0x9ac0000+0x1000`、disp_cc_base `0x9bb1000+0x4`、mdp_intf `0x998f000+0x300`；IRQ 4（父节点 `&mdss`） | DSI 控制器 0，`qcom,dsi-ctrl-hw-v2.10` |
| `dsi1: qcom,mdss_dsi_ctrl1@9ac3000` | `0x9ac3000+0x1000`、`0x9990000+0x300`；IRQ 5 | DSI 控制器 1 |
| `dsi_phy0: qcom,mdss_dsi_phy0@9ac1500` | `0x9ac1000+0xa00`、`0x9ac1500+0x400`、`0x9ac0200+0xa0` | `qcom,dsi-phy-v7.2`，同时是 DSI PLL 时钟提供者（`#clock-cells = <1>`） |
| `dsi_phy1: qcom,mdss_dsi_phy1@9ac4500` | `0x9ac4000+0xa00`、`0x9ac4500+0x400`、`0x9ac3200+0xa0` | 同上 |
| `dsi_pll_codes` | reg 全 0（bootloader 填充） | DSI PHY 的 PLL code 区，由 `pll_codes_region` 引用 |
| `dsi_panel_pwr_supply` | — | 面板三路供电：`vddio` 1.8 V、`vdd` 1.25 V、`vci` 3.0 V |

连接器（`mdss` 的 `connectors` 列表，6 项）：

| 节点 | 作用 |
|---|---|
| `dsi_display_primary` / `dsi_display_secondary` | `qcom,dsi-display`；主屏的 `qcom,dsi-ctrl = <&dsi0 &dsi1>`、`qcom,dsi-phy`、`qcom,dsi-default-panel = <&panel>`、TE GPIO `&tlmm 0x56` |
| `smmu_sde_unsec` / `smmu_sde_sec` | SDE 的 IOMMU 上下文，`iommus = <&apps_smmu 0x800 0x2>` / `<&apps_smmu 0x801 0x0>` |
| `wb_display0` / `wb_display1` | writeback 显示 |

时钟与互联（`mdss`）：

- `clocks = <&gcc 0x0f>, <&dispcc 0x06>, <&dispcc 0x08>, <&dispcc 0x52>, <&dispcc 0x46>, <&dispcc 0x47>`
  → `gcc_bus` / `iface_clk` / `ahb_swi_clk` / `vsync_clk` / `lut_clk` / `ssip_clk`
- `interconnects = <&mmss_noc 0x17 &gem_noc 0x22a>, <&mc_virt 0x03 &mc_virt 0x200>, <&gem_noc 0x02 &cnoc_cfg 0x207>`
  → data-bus0 / ebi-bus / reg-bus
- `mmcx-supply = <&pmh0110_d_s7_level>`；DSI 控制器另有 `vdda-1p2-supply`，PHY 有 `vdda-0p9-supply`
- `nvmem-cells = <&qfprom_ssip_config &qfprom_disp_qultiv_fuse>`（offset 0x148 / 0x144）

## 生成规则

1. **属性白名单**：只改写确实是 phandle 列表的属性 —— `clocks`、`interconnects`、
   `power-domains`、`iommus`、`phys`、`nvmem-cells`、`mboxes`、`resets`、
   `interrupts-extended`、`interrupt-parent`、`pinctrl-*`、`*-supply`、`*-supply-entries`、
   `*-gpio(s)`、`connectors`、`qcom,mdp`、`qcom,dsi-* `、`pll_codes_region`、`memory-region`。
   其余数值属性（`qcom,sde-*` 表）一个字节都不动 —— 早期版本对全文件扫 `<0x…>`，
   会把 LUT 表里的 0x45 当成 phandle，本次修的就是这个。
2. **按 provider 的 cells 数走查**：每个引用的参数格数取自目标节点的
   `#clock-cells`/`#interconnect-cells`/`#iommu-cells`/…，因此 `<&dispcc 0x06>` 的
   `0x06` 不会被误当成下一个 phandle。
3. **占位 provider**：显示子树之外的 19 个 provider（GCC、DISPCC、DISPCC_MX、RPMh 时钟、
   4 个 NoC、SMMU、SOCCP、2 个 qfprom cell、7 路 PMIC 稳压器）按 live 节点的
   compatible/reg/cell 属性生成占位节点，统一 `status = "disabled"`，节点上方注释保留
   stock 路径。真正的驱动落地后再替换。
4. **显式丢弃**（生成器里带原因，产物里以注释保留原值）：
   - 面板库：stock 的 `mdss` 下挂着 40 多个 `qcom,mdss_dsi_*` 参考面板，只保留本机
     BF375（在 `nx741j-panel-bf375.dtsi`）；
   - `dp_display` 与 `sde_cesta`：会把 USB/alt-mode 图和更多稳压器拉进来，`connectors`
     列表里对应两项一并去掉；
   - `pinctrl-0…3` 与 `pinctrl-names`：TLMM pin 状态，属 pinmux 步骤（名字列表单独留下没有意义）；
   - `memory-region`：SDE 的 reserved-memory，等 reserved-memory 建模后补。
5. `phandle`/`linux,phandle` 属性全部删除，由 dtc 重新分配。

## 编入方式

```dts
/include/ "sm8850.dtsi"
/include/ "nx741j-hw.dtsi"
/include/ "nx741j-mdss.dtsi"          /* mdss/dsi/phy + 占位 provider */
/include/ "nx741j-panel-bf375.dtsi"   /* &dsi0 { panel: panel@0 { … } } */
```

面板文件用 `&dsi0` 覆盖挂载，标签 `panel` 供 `qcom,dsi-default-panel` 使用；
`&tlmm` 来自 `nx741j-hw.dtsi`。DSI 控制器节点因此补了 `#address-cells = <1>`、
`#size-cells = <0>`（面板是它的子设备）。

## 验证

编译用与 `scripts/unixnu/devicetree.py` 相同的命令与 include 路径：

```sh
dtc -I dts -O dtb -o /tmp/hardware.dtb \
    -i /opt/work/nx741j-build/UniXNU-sm8850-common/dts \
    dts/nx741j.dts
```

| 项 | 结果 |
|---|---|
| dtc 编译 | ✅ 通过：硬件 DTB 5,614 B → 56,798 B（55.5 KiB），增量就是 mdss 子树与面板 |
| 新增告警 | 只有 vendor 风格节点的 `unit_address_vs_reg`（`vcm@0`、`timing@0`、`wb-display@0` 等），与 stock 树同现象；`reg_format`/`avoid_default_addr_size` 已通过补 cell 属性消除 |
| ADT | ✅ 未受影响：`scripts/make-adt.py` 用旧/新 DTB 生成的 ADT 均为 12972 B，`/arm-io` 内容一致（本节新增节点都不带 `unixnu,adt-name`） |
| 面板节点 | ✅ `/qcom,mdss_dsi_ctrl0@9ac0000/panel@0`，`compatible = "nubia,bf375-rm692h5"` |

`./unixnu payload` 的完整链路没有在本机跑：构建树的 `out/` 与 `dts/` 属于 root，
本会话的 WSL 用户没有免密 sudo。需要时用 root 执行：

```sh
cd /opt/work/nx741j-build/UniXNU
sudo ./unixnu payload -D /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
```

## 未完成

| 项 | 说明 |
|---|---|
| 驱动 | SDE/DSI/背光驱动未实现；ADT 里没有显示节点，XNU 仍用 GOP framebuffer |
| pinctrl | TE/复位/ESYNC 的 TLMM pin 状态未建模（`pinctrl-0…3` 被丢弃） |
| reserved-memory | SDE 的 `memory-region` 未建模；`dsi_pll_codes` 的 reg 全 0，待 bootloader 填充 |
| DP/CESTA | DisplayPort 与 HDCP 块未纳入（需要 USB/alt-mode 图） |
| 实机 | 层 4 仍未进内核，显示驱动只能到源码/主机级验证 |
