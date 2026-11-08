# 本地开发配置（nx741j）

对应官方[设备仓库 · 本地开发](https://unixnu.com/zh/porting/device-repository#本地开发)。

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
