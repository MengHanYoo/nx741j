# 步骤 4：解析并检查构建配置（2026-11-08）

对应官方 `porting/steps` 第 4 步。四条命令在构建树执行，设备路径 `../UniXNU-nx741j`。

## 命令与结果

| # | 命令 | 结果 |
|---|---|---|
| 1 | `./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j` | 写出 `out/nx741j/ios-16.7.16-20H392/nx741j/.config` |
| 2 | `./unixnu fetch -D ../UniXNU-nx741j` | 解析 submodules，写 `unixnu.lock`（与已提交版本一致，无 diff） |
| 3 | `./unixnu status --json -D ../UniXNU-nx741j` | `configured=true`、`schema=2`、20 个 module、6 个组件、63 条补丁、无本地覆盖 |
| 4 | `./unixnu tree -D ../UniXNU-nx741j` | 组装到 `out/nx741j/ios-16.7.16-20H392/nx741j/tree` |

## 解析出的配置层

| 层 | 路径 |
|---|---|
| arch | `profiles/arch/arm64` |
| soc | `out/feeds/UniXNU-sm8850-common-33433444`（feeds pin `825a5304`） |
| os | `profiles/os/ios-16.7.16-20H392` |
| device | `UniXNU-nx741j` |

## 组件实际 revision（来源：`status --json`）

| 组件 | revision |
|---|---|
| ArmDTPlatform | `417ff95d6a6e8607ddea9309ec45c3bfd4a67c60` |
| ArmCpu | `93a5030c5d983d0ef90f0fca93879d6e36ad1ba8` |
| SoftwareAES | `4a45326da5c2cd6fe6813d79ee43db20805628af` |
| XnuPrelink | `c645d76bb0ab8c506a76840ddf93a2276d419a0e` |
| DarwinBoot | `3c75734014cdab0acd9541e88057b7606ee7ae08` |
| GopFramebuffer | `e47c1b8de169cd2bd0b885c7d85e433010e3cdf2` |

## 结论与证据

- 依赖解析完成，构建树使用 `unixnu.lock` 固定的源码版本；
- 20 个 module 全部 `local=false`（无本地覆盖），`unixnu status` 未报告不可复现；
- 63 条 XNU 补丁按 `patch_order` 排序，前 6 条为 `aa-`…`af-` 系列；
- 主机证据：命令与输出见上表，设备仓库 revision `7cdf2738b8ee007a65fca59f1a36232fac520656`。

## 存疑项修正

`docs/SHARED-SUPPORT-SELECTION.md` 第 2 节此前记录的 ArmCpu/DarwinBoot/GopFramebuffer/XnuPrelink
四个 revision 来自旧快照，本次按 `status --json` 实际值更新。
