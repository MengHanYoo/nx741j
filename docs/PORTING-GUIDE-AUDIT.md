# 移植章节全量抓取分析（porting/* 及其超链接）

抓取：`https://unixnu.com/zh/porting/` 起，递归跟随站内 `/zh/` 链接，共 **85 页**（25 个 porting 页 +
60 个被引用页），正文落盘 `_tmp/porting-web/*.txt`，索引 `_index.json`。

## 1. 官方四阶段 / 八步 与我们的位置

| 官方步骤 | 结果要求 | 我们 |
|---|---|---|
| 1 收集设备输入 | 硬件清单 + 缺失输入 | ✅ `docs/HARDWARE-INVENTORY.md` |
| 2 选择共享支持 | 固定源码版本 | ✅ `feeds.toml` + components |
| 3 创建设备仓库 | 仓库完整、`board.example=false` | ✅ |
| 4 解析配置 | `status --json` 通过 | ✅ |
| 5 硬件描述与固件 | 两个树输入 + DSC/FDF 集成 | ✅（FV 满载 112 B 是唯一约束） |
| 6 按依赖顺序适配驱动 | 每项能力有配置和验证记录 | ⏳ 平台/存储做了一半（阶段 6 阻塞） |
| 7 构建并启动 | 镜像可启动 | ⏳ 1–5 阶段通过 |
| 8 完成并发布 | 适配记录 + 打包脚本 | ⬜ 未开始 |

## 2. bring-up 七阶段验收 vs 我们

| 阶段 | 官方验收 | 我们 |
|---|---|---|
| 1 UEFI | FD 大小匹配、header 合规、`fastboot boot`、framebuffer/串口、负载卷可见 | ⚠️ 全过但**不走 fastboot**（efisp） |
| 2 UniXNU | `UNIXNU:` 行、entry/driver/patch/ACPI/slice/kit 数量、菜单 | ✅ |
| 3 负载 | 配置路径、xnu 项、哈希一致、ADT 在侧 | ✅ |
| 4 DarwinBoot | `DARWINBOOT:` 行、VA/PA/入口、framebuffer、EL1、拒绝时回菜单 | ✅（拒绝时停 Fail 屏，刻意偏差） |
| 5 内核入口 | 版本/参数、页表、平台专家、五节点 | ✅ |
| 6 存储与根卷 | 链路速度+队列、OS/SystemOS、UUID 匹配、launchd | ❌ **阻塞在 UFS 接管** |
| 7 用户态 | launchd、Data 挂载、显示/控制台、boot-progress | ⬜ |

## 3. UFS 页的关键细节（直接指向阶段 6 的根因）

抓到的 `porting/ufs` 正文里有四条我们**没有实现**的硬性规则：

1. **UTRD 中断位是 dword 0 bit 24**：
   > 「只有 UTRD 中断位（dword 0 bit 24）置位的描述符才会置 UTRCS 并产生完成中断。只打开 UTRCE，按已清除的 doorbell 位找出完成的命令。」
   我们的 `dw0 = 0x10000001`（bit28 + **bit0**），**缺 bit24、多一个非法 bit0**；pandora 的可用驱动是 `bit28|dir<<25|bit24`。
2. **32 字节 UTRD、两条共用 64 字节缓存行**：
   > 「在非一致性的 stream 上，清理一项可能把旧副本写回，覆盖控制器为相邻项写入的完成状态。请求列表应放在写合并页上…或让每个槽独占一条缓存行。」
   我们复用固件列表页（`0xc3afa000`），且是 4 KiB 页——若该页被当普通缓存页做 dcache 清理，就会踩这条。
3. **SMMU bypass 有地址位宽约束**，且接管 apps SMMU 的驱动还要为 `dpu-scanout`、`wifi=2` 建 stage-1 窗口（默认关）。
4. **完成 = 中断 + 混合轮询**：先轮询 200 µs 再睡；轮询之间用 WFE（`native-ufs-wait=1`，上限 `native-ufs-spin-us`），
   不能直接 `IODelay`（每次阻塞 30–37 µs）。

官方给的 UFS 完成条件是 7 步串行验证：**寄存器访问 → DMA → 命令完成 → 已知块读取 → 根卷挂载 → 持久化 → 性能**。
我们现在卡在「DMA/命令完成」这一步。

## 4. 其余页面状态（一句话）

| 页面 | 要点 | 我们 |
|---|---|---|
| platform | CPU/中断资源、内存与 DMA、块设备与 OS 服务 | ✅ 平台专家已发布，块设备未到 |
| display | 面板时序、背光、GOP 交接 | ⏳ GOP 已有，内核显示驱动缺（`GopFramebuffer` 未进 archives） |
| input | 触控/按键/键盘三通道 | ⏳ Synaptics TCM 驱动未写 |
| gpu | Adreno 电源/GMU/ring/user client | ⬜ 未开始 |
| network/bluetooth/usb/baseband/power | 各自适配流程 | ⬜ 未开始 |
| driver-development | 驱动查找、接口、构建接入 | ✅ 已按此模式做 |
| packaging | 打包脚本规则 | ✅ 未做但非阻塞 |
| records | 适配记录格式与完成等级 | ⏳ `porting.toml` 存在，存储区待更新 |
| migration/soc-support | 迁移与 SoC 支持选择 | ✅ 不适用（新建仓库） |
| dual-system | 实验性双系统分区 | ✅ 用 efisp 方案 A |

## 5. 结论与优先级

1. **先修 UFS 描述符**：`dw0` 改为 `bit28|dir<<25|bit24`（去掉 bit0），这是文档明确点名的完成条件；
2. **请求列表放写合并页/独占缓存行**，避免清理覆写相邻项的完成状态；
3. 完成路径改成「中断 + 200 µs 轮询 + WFE」，接上 `native-ufs-wait`/`native-ufs-spin-us`；
4. SMMU：确认 bypass 地址位宽覆盖整个 DMA 范围，并为显示/WiFi 预留 stage-1 窗口（后续）；
5. 阶段 6 通过后再按完成条件顺序验证根卷 UUID 与 launchd；
6. 阶段 8（records + packaging）留到最后。
