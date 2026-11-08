# efisp 引导路线分析（本机实例）

背景：ABL 拒绝加载第三方 UEFI `boot.img`，本机改用 `efisp` 分区承载自研 PE loader。
本文以 NX741J 实际分区、镜像和日志为准，分析该路线的信任模型、硬约束与失败模式。
设备仓库 revision `c657995050fb072337f9d69edef09842dce7f6ec`。

## 1. 两条路径的对比

| 项 | `boot.img` 路径（被拒） | `efisp` 路径（在用） |
|---|---|---|
| 分区 | `boot_a` LUN4 LBA 119078（96 MiB） | `efisp` LUN4 LBA 748894（768 扇区 = **3 MiB**） |
| 由谁校验 | ABL + AVB（vbmeta 链、boot 头、GKI/vendor_boot 摘要） | ABL 只按 PE 加载，不查 AVB（efisp 不在 vbmeta 链） |
| 内容 | 我们的 BootShim+FD+DTB gzip | 自研 PE32+ ELF loader（约 197 KB 有效镜像 + 填充） |
| 入口 | BootShim（192 B，FD_BASE 0xff300000） | loader entry `0x1000`，image base 0 |
| 结果 | ABL 回退 fastboot / MemoryDump | ✅ 已验证：loader 从 efisp 启动，屏幕出现完整 DXE 日志 |

历史证据（来自 `ANALYSIS-EFISP-VENDORBOOT.md`）：`boot.img` 多次被 ABL 拒绝；官方 `efisp`
内容是 ZTE 版 Qualcomm **LinuxLoader**（3 MiB PE，`MZ@0xE58`，entry `0x1000`），说明
ABL 对 efisp 的加载契约就是"裸 PE + 分区即镜像"。

## 2. efisp 路线的信任模型

```
ABL（签名、AVB、反回滚）
  └─ 读 efisp（LUN4 LBA 748894）→ 按 PE 载入 → 跳 entry（x0 = FDT）
       └─ 我们的 loader（无签名校验）
            └─ rawdump LUN0 LBA 4743976: payload @0, FD @+64 MiB
                 └─ FD 拷到 0xFF300000 → 关 MMU/缓存 → EL1 跳入 UEFI
```

关键点：

1. **efisp 内容不受 AVB/签名保护**——这是路线成立的原因，也是最大信任缺口：任何能写
   efisp（9008/root）的代码都会在 EL1 执行。
2. ABL 仍执行它自己的校验（boot 头、vbmeta），但只针对 `boot_a`/`vendor_boot` 等受保护分区；
   efisp 是 ZTE 定义的"自定义引导槽"，其语义由 ABL 的 LinuxLoader 支持决定。
3. 本机进入该路径依赖工具箱的 **GBL/8E5 漏洞链**（免解 BL 写 efisp）；这不是通用解锁，
   OTA/回滚可能覆盖 efisp。
4. efisp 只有**单槽**（GPT 中无 `efisp_b`），写入失败没有 A/B 回退，只能靠 9008 恢复
   原厂 `efisp.img`（sha256 `4df183594a63af28…`）。

## 3. 硬约束（由本机镜像/分区实测）

| 约束 | 值 | 来源 |
|---|---|---|
| efisp 分区大小 | **3 MiB**（768 × 4096） | LUN4 GPT |
| PE 有效镜像 | ≈197 KB（`.text` 94,208 + `.data` 98,304 + `.reloc` 4,096） | `efisp.img` 解析 |
| 固件卷大小 | **4 MiB**（`FD_SIZE=0x400000`，`FD_BLOCKS=1024`） | `nx741j.dsc`/构建参数 |
| 固件卷加载基址 | `0xFF300000` | FDF/loader |
| rawdump 分区 | 512 MiB，LUN0 LBA 4743976 | GPT |
| payload 偏移 | rawdump +0，固定 64 MiB | `unixnu` payload 配置 |
| FD 偏移 | rawdump +64 MiB（16,384 个 4096 扇区） | loader/README |
| loader 栈 | 自带 32 KiB | `p5.c` |
| 跳转状态 | 关 MMU/D-cache/I-cache、`daifset`、`tlbi` | `p5.c` |

推论：

- **FD 不可能放进 efisp**（4 MiB > 3 MiB），payload（64 MiB）更不可能 ⇒ loader 必须从
  rawdump 取数据，这是该路线必须依赖 9008 写 rawdump 的根本原因。
- FD 尺寸被 `FD_SIZE` 固定为 4 MiB，若 UEFI 固件增长超过 4 MiB，需要同时改 FDF、DSC 和
  loader 的拷贝长度。
- boot.img 头部 v4 无 ramdisk 地址，ramdisk 只能紧跟 kernel（0x80158000 起），64 MiB payload
  会压到 0x81F00000 的 DTB 与 0x81000000 的 vendor ramdisk 区——即使 ABL 不拒绝，这条路径
  也装不下本方案的 payload（见 `ANALYSIS-EFISP-VENDORBOOT.md` §0.6）。

## 4. 失败模式

| 现象 | 原因 | 恢复 |
|---|---|---|
| ABL 回退 fastboot / MemoryDump | `boot.img` 被拒（本路线不使用它） | 不刷 boot.img |
| 屏幕无输出、fastboot 出现 | efisp 无有效 PE（写坏/清空） | 9008 写回 stock `efisp.img` |
| fastboot 可用但 fastbootd 异常 | efisp 内是 stock LinuxLoader，被我们的 loader 替换 | 同上 |
| loader 启动但固件崩溃 | FD 无效/基址不符（`DALSYS.efi` 后崩溃见 §18） | 重写 payload+FD，核对 `0xFF300000` |
| 固件正常、XNU 不启动 | 阶段 3/4（菜单→DarwinBoot）问题 | 层 4 探针（V67） |

## 5. 对本项目的影响

1. **`boot.img` 仅作归档**：`unixnu bootimg` 的产物不参与实机引导；所有固件更新走
   payload/FD → rawdump，loader 走 efisp。
2. **设备树来源分离**：loader 的 `x0=FDT` 只是保留 BootShim 约定，本固件不消费；
   UEFI 用 FDF 内嵌 `FdtBlob`，XNU 用 payload 内 `dts/board.dts` 生成的 ADT。
3. **无回滚保护**：efisp/rawdump 都不在 AVB 链内，升级失败只能 9008 恢复；发布脚本必须
   明确写盘顺序（payload → FD → efisp）与 stock 恢复镜像。
4. **安全边界**：该路线等价于在锁定设备上获得 EL1 代码执行；不要在此基础上叠加需要
   Verified Boot 的假设（例如依赖 vbmeta/dtbo 校验）。

## 6. 待验证问题

| 问题 | 方法 |
|---|---|
| ABL 对 efisp PE 的具体校验（大小/节/entry/reloc） | 用接近 3 MiB 的测试 PE 探测边界 |
| efisp 在 OTA 中是否被重写 | 抓一次 OTA payload 的分区列表，或对比 OTA 前后 efisp sha256 |
| ABL 传给 loader 的 `x0` 是否始终是 FDT | 在 loader 里打印 x0 指向的 magic（当前直接忽略） |
| rawdump 写入后的缓存一致性 | 写完后 ABL 侧是否需要 flush（9008 写已含 flush） |
| efisp 单槽损坏的恢复窗口 | 9008 是否总可进（Sahara 已多次验证） |

## 7. 参考

- `docs/ANALYSIS-EFISP-VENDORBOOT.md`（efisp/LinuxLoader/vendor_boot 物证）
- `docs/BOOT-CHAIN.md`（五阶段交接与验证状态）
- `loader/README.md`（写盘顺序、镜像 sha256、回滚方法）
- `docs/STORAGE-BRINGUP.md`、`docs/STEP6-UFS.md`（efisp 之后的固件内阶段）

## 8. 已实施的加固（loader p6）

| 问题 | p5 | p6（当前） |
|---|---|---|
| FD 头无效仍跳转 | 只打印 `fv first/fvh` | 校验 `_FVH` 与 FvLength（0x1000…4 MiB），失败 **返回 EFI 错误** |
| payload 读失败 | `break` 后用残缺数据继续 | 校验 `done == 64 MiB`，失败返回错误 |
| 拷贝不可见 | 打印拷贝后头 | 逐 8 字节回读比较，`copy diff` 非零则返回错误 |
| 写坏 efisp 后行为 | 死循环 stall | 打印原因并返回，ABL 可回退 fastboot（待实机确认） |

镜像：`loader/images/efisp-p6.img`，3 MiB，sha256
`a867866849b0ce943111523d5615d422857595e9b6761e1758dab584bee97bca`；PE 7,168 B，
`subsystem 10`、`machine 0xaa64`，含 `.reloc`。

仍未解决的固有限制：efisp 单槽无 A/B、无签名/回滚保护、rawdump/FD 写入依赖 9008
（p6 只能缩短"坏镜像导致死机"的窗口，不能替代备份与恢复流程）。
