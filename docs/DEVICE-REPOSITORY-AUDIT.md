# 设备仓库完成度审计（官方 device-repository 页）

依据 <https://unixnu.com/zh/porting/device-repository> 的布局与完成清单。

## 版本控制文件

| 文件 | 状态 | 说明 |
|---|---|---|
| `AGENTS.md` | ✅ | 工作区约定、OS 基准、组件 `os_abi` 要求 |
| `README.md` | ✅ | 设备范围、状态表、前置条件 |
| `.gitignore` | ✅ | 忽略 `local.toml`/`build`/`out`/`.cache` |
| `board.toml`/`porting.toml` | ✅ | schema 2；SoC/OS profile 一致；14 个 area |
| `feeds.toml`/`unixnu.lock` | ✅ | sm8850 固定提交；基础源由 UniXNU feeds 提供 |
| `configs/nx741j_defconfig` | ✅ | 实际被 `[kernel].defconfig` 引用 |
| `dts/nx741j.dts` | ✅ | 引入 `sm8850.dtsi` + `nx741j-hw.dtsi` |
| `patches/xnu/series` | ✅ | 设备补丁序列（当前为空文件） |
| `scripts/package.sh` | ✅ | 从干净检出产出 boot/payload |
| `proprietary-files.txt` | ✅ | 10 项全部有实测 sha256 |
| `package/` | ✅ | `firmware-overlay/`、`storage-bringup/`、`Kit.toml` |
| `local.toml.example` | ✅ | 源码覆盖示例 |
| `.github/workflows/*` | ⚠️ | 已按用户要求移到 `workflows-disabled/`；官方发布要求恢复 |
| `[ipsw]` | ✅ | URL/产品/版本/构建/设备类/chip/board + 成员 sha256 已配置 |
| `[firmware].preflight` | ✅ | `kits/nx741j/firmware-preflight.py` 自动应用 overlay 与 V68 vendored 驱动 |

## 解析与验证

```sh
./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j
./unixnu fetch -D ../UniXNU-nx741j
./unixnu status --json -D ../UniXNU-nx741j
./unixnu tree -D ../UniXNU-nx741j
python3 scripts/porting-record.py check --device ../UniXNU-nx741j
```

结果见提交说明；`kernel`/`payload`/`bootimg` 均已按本仓库产出。

## 差项与后续

1. 恢复 `.github/workflows/`（按官方发布要求）或明确记录永久禁用；
2. 若要 `unixnu ipsw fetch` 可用，补 `[ipsw]` 的 URL/身份/成员哈希；
3. 把板级 overlay 变成 `[firmware].preflight` 可调用的无参脚本（目前手动执行）。

## 验证输出（本轮）

```
unixnu defconfig nx741j_defconfig  → rc 0
unixnu fetch                      → rc 0
unixnu status --json              → configured=true, components 9, modules 24, patches 63
unixnu tree                       → rc 0
porting-record.py check           → 14 structured areas checked
kernel.macho                      sha256 e37bb06cfb2b1fb0e3a723c69f7ae07b72f546bcd14f571f59227d6193b07a87
payload.img                       sha256 22f8af8038529663150deb571a585127f3297be239040cf115501e866022e945
boot.img                          sha256 2e82d6e16aae40ed6d7fdc2f83a41779cd6733fb64b89b6f6acaa5696dd26ce6
```

## 本地固件侧改动（不在设备仓库内）

`UniXNUDxe.c` 的直启兜底与 `Nx741jKeysDxe` 的临时禁用位于 UniXNU 框架检出的
`package/UniXNUPkg/`，设备仓库目前无法通过 `[firmware].patches` 覆盖该目录（该键只作用于
共享 edk2 树）。复现 V68 需要保留这份本地修改；若要让仓库自包含，需要把这两个文件纳入
`package/firmware-overlay/` 并调整 `[firmware].packages` 的优先级。
