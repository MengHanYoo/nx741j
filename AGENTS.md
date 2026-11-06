# AGENTS.md - NX741J

## 工作区约定
- 构建产物/日志/临时文件放 E: 盘（E:\default-workspace\z80u-collect）。
- 固件树在 WSL：/opt/work/uefi-sheng-main；设备仓库：repos/UniXNU-nx741j。
- 不自动 git commit；仅在用户明确要求时提交。

## 已知硬件事实
- SoC：SM8850（canoe/Kaanapali），GPU：Adreno 840（gen80200 固件）。
- 显示：面板 bf375/rm692h5，原生 1440x3200；UEFI GOP 必须为 1216x2688。
- 存储：UFS；boot_a LBA 119078，uefi_a LBA 6，efisp LBA 748894（768 扇区）。
- SMEM：0x81D00000/0x200000，由 XBL 初始化（xbl_config_a.img），ABL 不参与。

## 双系统分区策略
| 方案 | 位置 | 说明 |
|---|---|---|
| A | efisp 分区 | 首选，独立 3 MB |
| B | userdata FAT 镜像 | 备选 |
| C | FV 内嵌 FAT | 自包含但需重建 |

当前选择：先 A。

## 构建命令
./unixnu status --json
./unixnu tree
./unixnu build firmware -D ../UniXNU-nx741j --uefi /opt/work/uefi-sheng-main
