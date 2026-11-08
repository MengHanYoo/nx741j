# 步骤 1 · 构建环境核对（nx741j）

依据官方[构建环境](https://unixnu.com/zh/guide/requirements)：主机工具、Linux 工具链、外部输入与本地配置。
设备仓库 revision `3ade08e9750afdcd65a4978b098e67af1a120f52`。

## 主机工具（WSL Ubuntu）

| 工具 | 状态 |
|---|---|
| Python | 3.14.4（要求 ≥ 3.11） |
| git / make / cmake / ninja / unifdef | 已安装 |
| LLVM / lld | `/usr/bin/clang`、`lld`（LLVM 21） |
| mtools / mtype | 已安装（payload 卷） |
| dtc | 已安装（设备树） |
| fastboot | 本次补装（`apt install fastboot`） |
| qemu-system-aarch64 | 已安装（可选冒烟） |
| macOS 专属步骤 | 不可用（`--skip-setup`、seed-data-grow）；本仓库 payload 用 `ramdisk = none`，不依赖 |

## Linux 工具链

```sh
scripts/linux/setup-toolchain.py --check
→ host dependencies ready: /usr/lib/llvm-21/bin/clang, /usr/bin/llvm-config
```

（clang/lld/cctools-port 由 `setup-toolchain.py` 安装，内核步骤已在用。）

## 本地构建输入（`unixnu.local.toml`，不入库）

| 键 | 值 | 状态 |
|---|---|---|
| `build.device` | `/opt/work/nx741j-build/UniXNU-nx741j` | OK |
| `build.uefi` | `/opt/work/uefi-sheng-main` | OK |
| `build.edk2` | `/opt/work/uefi-sheng-main/Mu_Basecore` | OK |
| `build.dtb` | `.../FdtBlob/kaanapali-zte-nx741j.dtb` | OK |
| `build.fd` | `Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd` | OK |
| `build.kernelcache` | `/opt/work/ipsw/kernelcache.arm64` | OK（预链接输入） |
| `build.trust_cache` | `ipsw/087-86593-047.dmg.trustcache` | OK |
| `build.jobs` / `target` / `kernel_args` | 16 / DEBUG / `-v serial=3` | OK |
| `build.unique_chip_id` | 本机真机值（不入库） | ✅ 已解析：`/sys/devices/soc0/serial_number = 160370627`（`0x098EE0C3`）零扩展为 16 位十六进制；payload 的 `kernel.macho.adt` 已含 `unique-chip-id = 0x098EE0C3` |
| `build.ipsw` | 未配置 | 只有解压后的 kernelcache，无 .ipsw 原包 |

## 验证

```sh
./unixnu status --json -D ../UniXNU-nx741j   # configured=true, components 6, modules 20
./unixnu bootimg -D ../UniXNU-nx741j         # boot.img 68,677,632 字节
```

## 缺项与存疑

| 项 | 下一步 |
|---|---|
| `unique_chip_id` | 本 ROM 的 `ro.boot.unique_chip_id` 为空；改用 QFPROM 派生的 socinfo 序列号（`160370627`）。若后续能在 bootloader 侧读到 QFPROM 原始 fuse，应交叉核对 |
| 无 `.ipsw` 原包 | 若要 `unixnu ipsw fetch` 需配置 `[ipsw] url` 或 `UNIXNU_IPSW` |
| macOS 步骤 | 本机不执行 `--skip-setup`/`seed-data-grow`；如需要走 GitHub macOS runner |

## 构建存储位置（9a1ab09）

WSL 发行版根盘（`/dev/sdd`）落在 C: 的 `ext4.vhdx`，重 IO 会拖慢构建。现将整个构建树迁到
**E: 上的 ext4 镜像**，路径保持不变：

| 项 | 值 |
|---|---|
| 镜像 | `E:\default-workspace\z80u-collect\work-ext4.img`（稀疏，118 GiB 逻辑容量，当前用 81 GiB） |
| 挂载点 | `/opt/work`（`/dev/loop0`，ext4） |
| 持久化 | `/etc/wsl.conf` 的 `[boot] command = /usr/local/bin/optwork-mount` |
| 手动挂载 | `/usr/local/bin/optwork-mount`（幂等：已挂载则跳过） |
| 临时目录 | `TMPDIR=/opt/work/tmp`（`/etc/profile.d/optwork-tmp.sh`） |

迁移验证：`./unixnu status --json`（configured=true、modules 20）与 `./unixnu payload`
（64 MiB）均成功；C: 上的旧副本已删除，根盘占用从 126 G 降到 58 G。

注意：若 WSL 版本不支持 `[boot] command`，每次启动后手动执行 `/usr/local/bin/optwork-mount`。
