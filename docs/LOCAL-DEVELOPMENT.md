# 本地开发配置（nx741j）

对应官方[设备仓库 · 本地开发](https://unixnu.com/zh/porting/device-repository#本地开发)。

## 2026-11-22 当前工作流

Linux 构建树是 ext4 中的 `/opt/work/nx741j-build/UniXNU`，其 VHDX 位于
`E:\wsl\Ubuntu\ext4.vhdx`。框架必须先从固定基线恢复：

```sh
./scripts/apply-framework-patch.sh /opt/work/nx741j-build/UniXNU
./scripts/sync-framework-to-wsl.sh /opt/work/nx741j-build/UniXNU
```

`sync-framework-to-wsl.sh` 会先调用应用脚本；若框架已是精确补丁结果则跳过。
payload 的 trust cache 不应直接指向 IPSW 的 DER/IM4P 文件，使用：

```sh
./scripts/prepare-local-inputs.sh
# unixnu.local.toml: trust_cache = ".../build/trustcache.bundle"
```

当前本地输入、补丁与验收边界见 `docs/INTEGRATION-2026-11-22.md`。

## 已配置内容

| 项 | 值 | 说明 |
|---|---|---|
| 设备仓库 | `UniXNU-nx741j` | 当前工作仓库 |
| `local.toml` | `[[source]] name="sm8850" kind="soc" local="../UniXNU-sm8850-common"` | Git 忽略，不进入提交 |
| SoC 本地检出 | `../UniXNU-sm8850-common` @ `825a5304e2b72b55ac67501f56d12cc905f2f458` | 与 `feeds.toml` 的 pin 一致 |
| 构建树设备目录 | `out/<device>/<os_profile>/<config>/` 对应的 `-D` 目录 | 需复制一份 `local.toml` 才会生效 |

## 生效步骤

```sh
# 设备仓库内（相对路径按 local.toml 所在目录解析）
cp local.toml ../<构建树>/UniXNU-nx741j/local.toml

DEVICE_DIR=../UniXNU-nx741j
./unixnu defconfig nx741j_defconfig -D "$DEVICE_DIR"
./unixnu fetch -D "$DEVICE_DIR"
./unixnu status --json -D "$DEVICE_DIR" | python3 -c \
  'import json,sys; d=json.load(sys.stdin); print([m for m in d["modules"] if m.get("name")=="sm8850"])'
```

期望：`sm8850` 的 `local` 为 `true`；`unixnu.lock` 中该项标记为本地、不固定提交。
提交到仓库的 `unixnu.lock` 保持 pin（本仓库当前未变），仅构建树副本带本地标记。

## 本机资源分配（2026-11-09）

| 项 | 值 | 位置 |
|---|---|---|
| 主机 | Ryzen 9 5950X（16 核 / 32 线程）、31.9 GB、电源方案"卓越性能" | Windows |
| WSL2 可见 CPU | **24** | `%USERPROFILE%\.wslconfig` → `[wsl2] processors=24` |
| WSL2 内存 / swap | 20 GB / 8 GB | 同上；swap 文件已放在 E 盘 |
| 内核构建并行度 | **`-j 16`（8 核 16 线程）** | 构建树的 `unixnu.local.toml` → `jobs = 16` |

Linux 构建树的磁盘文件位于 `E:\wsl\Ubuntu\ext4.vhdx`；在 WSL 内仍通过 ext4 路径构建，
不要把 `.wslconfig` 迁移成直接在 `E:\default-workspace` 的 9p 目录中编译。

验证：`nproc` = 24；构建时 `ps` 抓到 `make … -j16`。改并行度只需改 `unixnu.local.toml` 的
`jobs`（或命令行 `-j N`），改 VM 规格需编辑 `.wslconfig` 后 `wsl --shutdown`。

ext4 构建树上的实测：`./unixnu kernel -j 16` 全量 **158 s**（drvfs 上 25 分钟未完成）。

## 可写构建树（本机 WSL，2026-11-09）

`/opt/work/nx741j-build` 归 root，非 root 会话写不了 `out/`。复制一份到 E 盘即可构建：

```sh
cp -a /opt/work/nx741j-build/UniXNU /mnt/e/unixnu-build/UniXNU    # 1.3 GiB，约 6 分钟
cd /mnt/e/unixnu-build/UniXNU
./unixnu payload -D /mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j
```

注意事项：

- 工具链仍在 `/opt/unixnu-linux`（只读可用），构建树里的 `toolchain` 只是软链；
- `tree/kits/nx741j` 指向设备仓库的 `package/`，但 `tree/scripts/` 是设备脚本的**副本**：
  改完 `scripts/*.py` 要 `./unixnu tree`（或手工 cp）才会进构建；
- 树内 `build/` 是真正的编译输出（`libShengUfs.a`、`storage-bringup/` 等）。

复制到 drvfs（`/mnt/e`）后有两个坑，已经踩过：

| 现象 | 原因 | 处理 |
|---|---|---|
| `build-corecrypto-14.py` 报 "The crypto reference must stay immutable" | drvfs 把所有文件报成 `0777`，git 认为 1798 个文件 mode 变了 | `git config --global core.fileMode false`（并对 `refer/corecrypto-2023` 单独设一次） |
| 同脚本报 `Conflicting header: cc.h` | 复制的 `build/corecrypto-2023` 里 235 个软链指向 `/opt/...` 旧路径 | `rm -rf tree/build/corecrypto-2023` 让它重新生成 |
| `archive-11` 报 `The libpthread reference must remain immutable` | 检查的是 `tree/refer/libpthread`（不是 `.cache/libpthread-work`），它同样 `core.fileMode=true`，203 个 mode 变更 | 对 `tree/refer/*` 每个仓库都 `git config --local core.fileMode false`（`refer` 下还有 xnu、edk2、libdispatch、IOStorageFamily…） |
| 单独跑 `build-libpthread-kernel.py` 报 `TargetConditionals.h not found` | 直接在 shell 里跑没带 `UNIXNU_SDKROOT`，`xcrun --show-sdk-path` 回落到字面量 `iphoneos` | 用 `./unixnu kernel` 跑（它注入 `UNIXNU_SDKROOT=fakeroot` 等一整套环境），或照 `scripts/unixnu/kernel.py` 手动导出 |

## 覆盖优先级（官方）

1. `--local-module [KIND:]NAME=PATH`
2. `--local-<kind> DIR`
3. `--local DIR`
4. `feeds.toml` 的 `local` 键或 `local.toml` 条目
5. 设备仓库
6. 所选 SoC 仓库
7. 基础仓库
8. `feeds.toml` 的 git 源码

## 证据

- `./unixnu fetch` → `== wrote …/UniXNU-nx741j/unixnu.lock`
- `./unixnu status --json` → `{'name': 'sm8850', 'kind': 'soc', 'local': True}`
- 仓库内 `git diff --stat unixnu.lock` 为空（提交的锁定版本不变）
- 设备仓库 revision `4cce38f4a969a133c9eca267926aaee52f786c36`
