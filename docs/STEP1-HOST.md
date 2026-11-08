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
| `build.unique_chip_id` | `0000000000000001` | **存疑**：占位值，必须替换为真机 fuse 值 |
| `build.ipsw` | 未配置 | 只有解压后的 kernelcache，无 .ipsw 原包 |

## 验证

```sh
./unixnu status --json -D ../UniXNU-nx741j   # configured=true, components 6, modules 20
./unixnu bootimg -D ../UniXNU-nx741j         # boot.img 68,677,632 字节
```

## 缺项与存疑

| 项 | 下一步 |
|---|---|
| `unique_chip_id` 占位 | 设备在线时 `adb shell getprop ro.boot.unique_chip_id` 取真机值后替换 |
| 无 `.ipsw` 原包 | 若要 `unixnu ipsw fetch` 需配置 `[ipsw] url` 或 `UNIXNU_IPSW` |
| macOS 步骤 | 本机不执行 `--skip-setup`/`seed-data-grow`；如需要走 GitHub macOS runner |
