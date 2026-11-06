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

## 执行与网络约束（硬性）

- **一切构建、编译、刷机、测试都在本机执行**（Windows + WSL `Ubuntu`，32 vCPU / 27 GB）。
  DeepSeek Harness 只用于思考、编排与记录，不承担任何远程构建/编译。
- **构建过程必须离线可完成**：源码只允许来自本地镜像缓存 `/opt/ghcache`（git `insteadOf`
  已指向该缓存）或已 fetch 的 `out/feeds/`；禁止构建时临时拉取网络资源。
- 需要新源码时，先把仓库缓存进 `/opt/ghcache`（在线一次），再离线构建。
- 固件构建：`bash firmware/smem-stub/build.sh`（EDK2 BaseTools，本地树 `/opt/work/uefi-sheng-main`）。
- 内核构建：`./unixnu build kernel`（本地 WSL，clang 24 + cctools 在 `/opt/unixnu-linux`）。
- 刷机/取数：`wsl/write_img.ps1`、`adb`（本地 USB，不经网络）。

## 环境事实

| 项 | 值 |
|---|---|
| WSL 发行版 | Ubuntu 26.04（`-d Ubuntu`，root） |
| CPU / 内存 | 32 vCPU / 27 GB |
| 工具链 | `/opt/unixnu-linux`（clang 24 + 38 个 Mach-O 工具，2026-11-06 装好） |
| 源码缓存 | `/opt/ghcache/github.com/**`（xnu 186 MB 等 9 个仓库） |
| 固件树 | `/opt/work/uefi-sheng-main` |
| 产物盘 | `E:\default-workspace\z80u-collect`（`out/` 在 E 盘） |
