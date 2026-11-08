# 步骤 2 · 选择共享支持（SM8850 / nx741j）

依据 [组件目录](https://unixnu.com/zh/reference/components)、
[SoC 支持](https://unixnu.com/zh/porting/soc-support)、
[XNU 源码与补丁](https://unixnu.com/zh/xnu/source-and-patches) 与
[预链接](https://unixnu.com/zh/concepts/prelink)。设备仓库 revision `6de67b8359cf76c76d59f7ccaee4eda950547d24`。

## 组件选择（components）

| 项 | 状态 |
|---|---|
| 已选组件 | `ArmDTPlatform`、`ArmCpu`、`SoftwareAES`、`XnuPrelink`、`DarwinBoot`、`GopFramebuffer` |
| 解析结果 | components 6、modules 20、patches 63 |
| 目录核对 | 28 项逐条审计见 `COMPONENT-SELECTION.md` |
| 本地覆盖 | `sm8850`（`local.toml`）、`ShengUfs-canoe`（已备，未选中） |
| 待适配 | `ShengUfs` 需 storage ABI 归档；`ShengNovatekTouch` 不适用（实机 Synaptics TCM）；`Ath12kWifi/QcaBluetooth` 目标 WCN7850 与实机 WCN7880 不符 |
| `ArmSmmu4K` | 故意不选：catalog 中为 embedded 阶段、无 build/outputs/依赖（仅硬件能力标记），选了不会产生归档或代码；SMMU 由预链接的 IODART/平台代码覆盖 |

## SoC 共享支持（soc-support）

| 项 | 状态 |
|---|---|
| 仓库 | `MengHanYoo/UniXNU-sm8850-common`，本地覆盖于 `../UniXNU-sm8850-common` |
| 结构 | ✅ `AGENTS.md`、`README.md`、`soc.toml`（schema 2）、`configs/sm8850.config`、`dts/sm8850.dtsi`、`patches/xnu/series`、`kits/Sm8850Platform/` |
| 数据来源 | `soc.toml` 固定实机 DTB：`source_repository = "device stock DT (FdtBlob/kaanapali-zte-nx741j.dtb)"`，`source_revision = a5c109c0…` |
| 共享资源 | 8 核 MPIDR、`psci method = smc`、GICv3 `0x17000000+0x10000` / `0x17080000+0x200000` stride `0x40000`、19.2 MHz timer |
| 配置片段 | `board.toml [kernel] config_fragments` 含 `soc:configs/sm8850.config`（当前为注释，组件开关不放在 SoC 片段） |
| 补丁序 | `patch_series` 含 `soc:patches/xnu/series`（空） |
| DTS 引用 | `[device_tree] include_dirs = ["soc:dts"]`，板级 DTS `/include/ "sm8850.dtsi"` |
| 固定版本 | `feeds.toml` pin `825a5304`；本地仓库已更新到 `5ffa761`（**尚未推到 feeds 指向的 GitHub，本地覆盖生效**） |

## XNU 源码与补丁

| 项 | 状态 |
|---|---|
| XNU commit | `1b191cb58250d0705d8a51287127505aa4bc0789`（`unixnu.lock`） |
| 补丁顺序 | arch 63 条；`soc/OS/device` series 文件存在但为空（官方允许空 series，按顺序保留） |
| 4 KiB 页 | `build-xnu-4k-baseline.sh` + defconfig，内核按 4K 页构建 |
| 构建 | `kernel.macho` 51,691,520 B，sha256 `d082e1f7f749567df3a75ef6e6cc60c27a4fa846a8532256735756d0665b195d` |

## 预链接（prelink）

| 项 | 状态 |
|---|---|
| XnuPrelink | `c645d76bb0ab8c506a76840ddf93a2276d419a0e` |
| kernelcache pin | `2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221`（OS profile） |
| slide | `-0x1000000` |
| 预链接报告 | 11 个组件、23,833 bindings、`runtime_qualified=false`、`abi_qualified=false` |
| 组件 pin | ArmDTPlatform `417ff95d`、ArmCpu `93a5030c`、SoftwareAES `4a45326d`、DarwinBoot `3c757340`、GopFramebuffer `e47c1b8d` |

## 构建验证

```sh
./unixnu status --json -D ../UniXNU-nx741j   # configured=true, components 6, modules 20, patches 63
./unixnu kernel -D ../UniXNU-nx741j          # kernel.macho 51,691,520 B
./unixnu payload -D ../UniXNU-nx741j         # payload.img 67,108,864 B
./unixnu bootimg -D ../UniXNU-nx741j         # boot.img 68,677,632 B
```

## 存疑

| 项 | 说明 |
|---|---|
| `early_security_init` | ✅ 已显式置空且注释改为"非待定"：roots 不含 AMFI，预链接报告 `early_security_initializers = {}`；接入 AMFI 时再填 `identifier=address` |
| SoC pin 漂移 | 已验证 GitHub 仓库需要认证（`ls-remote` 要求用户名，仓库私有/不存在），无法在本机推送。策略：`feeds.toml` 保持 `825a5304`，结构修复在本地 `5ffa761`（已推本地镜像），本机构建走 `local.toml`；公开前需你执行一次 `git push` 到该仓库 |
| `runtime_qualified` / `abi_qualified` | 均 false：必须实机启动才能资格化；层 4 探针（V67）按你的要求暂缓，因此保持 ❌ 阻塞 |
| SoC 配置片段 | 实机数据仍缺 GPU/显示/存储等共享资源，当前片段故意留空 |

## 设备仓库 revision

`6de67b8359cf76c76d59f7ccaee4eda950547d24`
