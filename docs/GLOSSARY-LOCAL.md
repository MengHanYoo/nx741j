# 本地术语补充表（GLOSSARY-LOCAL）

官方术语表（`reference/glossary`）覆盖 UniXNU 通用词；本仓库还大量使用 **NX741J 引导链与高通平台**的术语，
官方表未收录。本文件是补充，不替代官方表：官方表已有的词（ADT、ABL、BootShim、userdata、SystemOS、
SHNGROOT、SMMU 等）仍按官方写法。

## 本机引导链

| 术语 | 含义 | 与官方术语表的关系 |
|---|---|---|
| `efisp` | UEFI 分区槽（LUN4，LBA 748894，3 MiB），存放自研 EFI loader；本机唯一验证可执行自写 PE 的槽 | 官方无此词；对应官方的「启动链」之外的厂商槽位 |
| `rawdump` | 大容量分区（LUN0，LBA 4743976，512 MiB），承载 `payload.img`（+0）与固件卷（+64 MiB）的载体 | 官方无此词；本机用它替代 `boot.img` 承载 payload 与 FD |
| `loader` | 写入 `efisp` 的 EFI 应用（`loader/images/efisp-p5.img`），负责读 `rawdump`、跳入固件卷 | 官方「BootShim」是另一条路径；本机不用 BootShim |
| `Mu-Silicium` / `Silicium` | EDK II 固件项目，产出 `SILICIUM_UEFI.fd` | 官方称「固件卷」（firmware volume），文件名即 `SILICIUM_UEFI.fd` |
| `FD` | 固件卷的简称（`SILICIUM_UEFI.fd`，4 MiB） | 等同官方「固件卷」；正文首次出现建议写「固件卷（FD）」 |
| `payload.img` | payload 的 FAT 卷镜像（本机 64 MiB，卷标 `NX741J`） | 官方「payload」术语的产物；`unixnu.toml` 在卷内 |
| `ADT sidecar` | `kernel.macho.adt`，内核旁的 Apple 设备树 | 官方术语表已收录（`ADT sidecar`） |

## 高通平台 / 下载模式

| 术语 | 含义 |
|---|---|
| `9008` / `Sahara` / `firehose` | Qualcomm 紧急下载模式与两段协议；本机刷机入口 |
| `XBL` | Qualcomm 早期引导加载器（在 `ABL` 之前） |
| `LUN` | UFS 逻辑单元号（本机 efisp 在 LUN4，ztecfg 在 LUN5） |
| `UFS` | Universal Flash Storage，本机主存储 |
| `GPT` | GUID 分区表 |
| `TLMM` | Qualcomm 顶层多路复用 GPIO 控制器（触控 IRQ 158、reset 66） |
| `PDC` | Power Domain Controller，中断唤醒控制器（`0xB220000`） |
| `IMEM` | 片内 SRAM 区（`board_id@0x60` 等） |
| `SMEM` | Qualcomm 共享内存（`0x81D00000`，2 MiB，`no-map`） |
| `DALSYS` | Qualcomm DAL 系统服务 |
| `IOMMU` / `SMMU` | I/O 内存管理单元（官方术语表用 `SMMU`，本机两者混用，建议统一写 `SMMU`） |
| `DMA` | 直接内存访问 |
| `fastbootd` | fastboot 的用户空间模式（`fastboot reboot fastboot` 进入） |
| `cryptex` | Apple 的加密卷容器格式（`SystemOS` 的载体） |

## 用词规则（本仓库）

1. 官方术语表已有的词按官方写法：`trust cache`（不写 trustcache）、`预链接`、`固件卷`、`设备仓库`、
   `ADT sidecar`、`SMMU`、`payload`；
2. 命令/补丁名/文件名中的连字符写法不改：`--trust-cache`、`native-trust-cache-runtime`、
   `087-*.dmg.trustcache`、`[payload] trust_cache_sha256`；
3. 新词先进本文件再使用；官方表已收录的不重复定义，只给链接。
