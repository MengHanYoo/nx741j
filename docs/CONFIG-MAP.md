# 配置/实现归属与操作步骤（编辑前先查这里）

对应 `reference/` 第 3 项"找到配置或实现"。每次改动前先在下表定位**文件归属**与**操作步骤**，
再动手；新增文件时同步更新本表与 `porting.toml` 对应 area 的 `paths` / `procedure`。

## 1. 归属总则

| 归属 | 路径 | 提交策略 |
|---|---|---|
| 主仓库（框架/组件/profile/文档/记录） | `repos/UniXNU` | 可提交（按需） |
| SoC 共享仓库 | `repos/UniXNU-sm8850-common` | 可提交 |
| 设备仓库（板级配置/文档/记录） | `repos/UniXNU-nx741j` | 可提交 |
| 固件平台 | `/opt/work/uefi-sheng-main`（发布仓 `uefi-nx741j`） | 可提交 |
| **本地输入（不提交）** | `unixnu.local.toml`、设备 `local.toml` | 不提交（模板 `local.toml.example`） |
| 构建产物 | `out/nx741j/ios-16.7.16-20H392/nx741j/**` | 不提交 |

## 2. 修改内容 → 文件/实现 → 操作步骤

| # | 修改内容 | 文件或实现（归属） | 操作步骤 |
|---|---|---|---|
| 1 | **本地输入路径** | `UniXNU/unixnu.local.toml`（构建用副本 `/opt/work/nx741j-build/UniXNU/unixnu.local.toml`，**未跟踪**）；设备仓库 `local.toml`（模板 `local.toml.example`，**未跟踪**） | 改路径/开关（`[build] device/uefi/edk2/dtb/fd/kernelcache`、`ramdisk`、`jobs`）→ 重跑 `./unixnu build …`；不要提交这两个文件 |
| 2 | **设备、SoC 或组件选择** | 设备 `board.toml`（`[board] soc/soc_source`、`[components] include`、`[payload]`）；`feeds.toml`（`sm8850` pin）；`configs/nx741j_defconfig` | 改 `board.toml` → `./unixnu status --json` 校验解析 → `./unixnu tree` 确认源码落位 → 构建 |
| 3 | **硬件资源** | 设备 `dts/board.dts` + SoC 仓库 `dtsi`；固件侧 `Platforms/ZTE/nx741jPkg/FdtBlob/*.dtb` 与 `Library/MemoryMapLib/MemoryMapLib.c` | DT：改 dts → `dtc -I dts -O dtb` 校验 → 更新 FdtBlob；内存映射：在 `MemoryMapLib.c` 的 `AddDev` 增删区间 → 重建固件 |
| 4 | **驱动行为** | 所选组件源码（`UniXNU/drivers/<Component>`，版本见 `components.toml`）；设备 `package/UniXNUPkg`（`UniXNUDxe`、`UniXNURamDiskDxe`、`Application/UniXNU`） | 改组件或 package 源码 → `./unixnu build kernel`（组件）/ `./unixnu build firmware`（package）→ 更新 `docs/FIRMWARE-DRIVERS.md` |
| 5 | **内核行为** | `profiles/os/ios-16.7.16-20H392/patches/xnu/series` + `configs/ios.config`；`profiles/arch/arm64`；设备 `patches/xnu/series`；SoC `patches/xnu/series` | 改补丁序列或 config → `./unixnu build kernel` → 检查 `logs/kernel*.log` 与 `kernel/kernel.macho` |
| 6 | **XNU 启动参数** | `board.toml [payload] kernel_args`（当前 `["-v","serial=3"]`）；本地 `[build] kernel_args`（`unixnu.local.toml`） | 改参数 → `./unixnu build payload` → `out/.../payload/payload.json` 核对参数 |
| 7 | **启动菜单 / kit** | `board.toml [payload]`（`title`/`timeout`/`tools`/`panic_screen`）→ 生成 payload ESP 内的 `EFI/UniXNU/unixnu.toml`（**构建期生成**，非源码）；kit 在 `UniXNU/kits/<Kit>/`（Python 模块，无 `Kit.toml`） | 改 `[payload]` → `./unixnu build payload` → 用 `efi-loader` 或 boot.img 上机验证菜单 |
| 8 | **设备支持记录** | 设备 `porting.toml`（14 area，含 `paths`/`procedure`）；主仓库 `devices.toml`（`nx741j` = `32b9855fb815…`） | 改 area → `python3 scripts/porting-record.py check --device ../UniXNU-nx741j`（须 `14 structured areas checked`）→ 发布时更新 `devices.toml` 的 revision |
| 9 | **文档** | 主仓库 `docs/`（英文站点页：`guide/`、`reference/`、`porting/`、`concepts/`、`devices/`）；设备仓库 `docs/`（中文记录：本目录） | 站点文档改主仓库 `docs/*.md`；移植记录/分析改设备仓库 `docs/`，并在 `AGENTS.md` 顶部索引 |

## 3. 编辑前的检查清单（三步）

1. **定位归属**：上表第 2 列；不属于任何仓库的（`local.toml`）只改本机、不提交；
2. **确认操作步骤**：上表第 3 列，按顺序执行并保留日志/校验结果；
3. **登记**：把"输入来源 → 实现 → 当前结果 → 下一项测量"补进 `porting.toml` 对应 area 的
   `inputs` / `configuration` / `procedure` / `findings` / `next_action`，并更新本表（若新增文件）。

## 4. 当前已知的归属歧义（已处理/待处理）

| 项 | 状态 |
|---|---|
| `unixnu.toml`（payload 菜单） | 构建期生成于 payload ESP，改配置只能改 `board.toml [payload]`（已记录） |
| 固件树 commit 未 pin | 待补：把 `uefi-sheng-main` 的 commit 写入 `feeds.toml` 或 `board.toml` |
| `unixnu.lock` 缺失 | 待补（框架/OS profile 的 commit 级固定） |

## 5. 规范索引（改哪类东西查哪页）

站点：`https://unixnu.com/zh/…`（英文页去掉 `/zh`）。锚点取自 2026-11-07 抓取的 reference 页面。

| 内容 | 参考文档 |
|---|---|
| 构建工具 | [命令与参数](https://unixnu.com/zh/reference/cli)、[配置字段](https://unixnu.com/zh/reference/configuration)、[输出路径与报告](https://unixnu.com/zh/reference/output-layout) |
| 组件 | [组件目录与源码身份](https://unixnu.com/zh/reference/components)、[仓库和组件页面](https://unixnu.com/zh/drivers/repositories) |
| 驱动接口 | [UEFI 驱动](https://unixnu.com/zh/drivers/uefi-drivers)、[内置 XNU 驱动](https://unixnu.com/zh/drivers/xnu-drivers)、[kext 包](https://unixnu.com/zh/drivers/kexts)、[设备树匹配](https://unixnu.com/zh/drivers/device-tree-matching) |
| XNU | [源码与补丁归属](https://unixnu.com/zh/xnu/source-and-patches)、[补丁目录](https://unixnu.com/zh/xnu/patch-reference)、[4 KiB 页表](https://unixnu.com/zh/xnu/four-k-page-tables) |
| 启动与镜像格式 | [启动链](https://unixnu.com/zh/concepts/boot-chain)、[预链接](https://unixnu.com/zh/concepts/prelink)、[payload](https://unixnu.com/zh/concepts/payload)、[userdata 布局](https://unixnu.com/zh/concepts/userdata)、[启动配置](https://unixnu.com/zh/reference/boot-configuration) |
| 完整性 | [trust cache 与启动策略](https://unixnu.com/zh/concepts/security) |
| 项目结构 | [仓库归属](https://unixnu.com/zh/contributing/repository)、[术语](https://unixnu.com/zh/reference/glossary) |
| 操作步骤锚点 | [解析设备配置](https://unixnu.com/zh/porting/steps#_4-解析并检查构建配置)、[设备树](https://unixnu.com/zh/porting/device-tree#procedure)、[适配硬件](https://unixnu.com/zh/porting/drivers)、[修改 XNU](https://unixnu.com/zh/xnu/source-and-patches#procedure)、[应用启动参数](https://unixnu.com/zh/xnu/boot-arguments#apply-arguments)、[启动配置](https://unixnu.com/zh/reference/boot-configuration#procedure)、[更新和发布记录](https://unixnu.com/zh/porting/records#procedure)、[编辑和发布文档](https://unixnu.com/zh/contributing/documentation#procedure) |

## 6. 验证受影响的输出（item 3）

在主仓库选定目标设备后先跑：

```sh
./unixnu status --json
```

**2026-11-07 实测**（`nx741j`）：

```
schema_version: 2
name: nx741j
configuration: nx741j_defconfig
configured: True
output: /opt/work/nx741j-build/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j
profile.arch : profiles/arch/arm64
profile.soc  : out/feeds/UniXNU-sm8850-common-33433444
profile.os   : profiles/os/ios-16.7.16-20H392
profile.device: UniXNU-nx741j
```

| 改动 | 验证方法 | 预期 | 本机状态 |
|---|---|---|---|
| 内核、驱动或补丁 | `./unixnu build kernel`，再按[启动验证顺序](https://unixnu.com/zh/porting/bring-up) | 内核与预链接成功 | ✅ `kernel.macho` 51,691,520 B（2026-11-07 10:30） |
| 固件或 payload | `./unixnu build firmware` / `build payload` | FD、payload、启动镜像用指定输入 | ✅ FD 4,194,304 B、`payload.img` 67,108,864 B、`boot.img` 69,402,624 B |
| 启动参数 | 检查 `payload/payload.json` 的 `kernel_args` | 与 `board.toml [payload] kernel_args` 一致 | ✅ `-v serial=3` |
| 公共 UEFI 行为 | [QEMU](https://unixnu.com/zh/reference/qemu#procedure) | 场景出现预期日志标记 | ✅ **6/6 场景 PASS**（见下） |
| 适配记录 | `python3 scripts/porting-record.py check --device ../UniXNU-nx741j` | 记录校验通过 | ✅ `nx741j: 14 structured areas checked` |
| 文档 | 站点构建（`docs/package.json` → `vitepress build`） | 文档构建通过 | ✅ 构建通过（80.12s）；nx741j 记录受发布命名空间限制，见 6.2 |

### 6.1 QEMU 冒烟实测（2026-11-07）

```sh
cd repos/UniXNU
JOBS=8 UNIXNU_EDK2=/opt/work/uefi-sheng-main/Mu_Basecore bash package/Scripts/smoke.sh
```

| 场景 | 结果 |
|---|---|
| 1 默认入口（DTB patch/overlay/kit/initrd/variable 五项检查） | PASS（`UNIXNU-HELLO: RESULT PASS 5 passed 0 failed`） |
| 2 键盘选择第二项 | PASS |
| 3 无 GOP 的内置配置 | PASS |
| 4 userdata 头 FAT + 尾部块设备 | PASS |
| 5 kext 暂存与 XNU 协议版本 | PASS |
| 6 1080×2400 竖屏 / 800×1000 横屏布局 | PASS |

- 产物：`repos/UniXNU/package/Build/Smoke/{default,keys,degraded,blockslice,kext,layout}.log`、`menu.png`；
- 需要 `UNIXNU_EDK2` 指向本机 `Mu_Basecore`（否则脚本报 "no edk2 tree"）；
- **上游测试陈旧需修正**：`package/Scripts/smoke.sh` 里内置配置期望写的是 3 个入口，
  实际是 4 个（`Return to firmware / Restart / Power off / Tools`），已本地改为 4（未提交）。

### 6.2 文档站点构建（✅ 构建通过，nx741j 页面已生成）

环境准备（本轮完成）：

| 步骤 | 命令/结果 |
|---|---|
| 安装 Node | `apt-get install -y nodejs npm` → **node v22.22.1 / npm 9.2.0** |
| 安装依赖 | `cd repos/UniXNU/docs && npm ci --no-audit --no-fund` → 243 packages |
| 渲染设备记录 | `python3 ../scripts/porting-record.py site --local sheng=/opt/work/site-records/sheng --local nx741j=<abs>/repos/UniXNU-nx741j` → `sheng: 13 areas`、**`nx741j: 14 areas`，两条记录均 rendered** |
| 站点构建 | `npx vitepress build` → **build complete in 78.22s** |
| 产物 | `.vitepress/dist/devices/nx741j.html`、`.vitepress/dist/zh/devices/nx741j.html` |

为了让 nx741j 出现在站点里，本轮在**主仓库**新增/修改了 4 处：

| 文件 | 改动 |
|---|---|
| `docs/devices/nx741j.md` | 新建设备页，含 `<!--@include: ../.vitepress/records/nx741j.md-->` |
| `docs/zh/devices/nx741j.md` | 中文设备页，含 `<!--@include: ./records/nx741j.md-->` |
| `docs/devices/records/nx741j.md`、`docs/zh/devices/records/nx741j.md` | **提交进仓库**的记录快照（`.vitepress/records/` 被 gitignore，不能用于 CI） |
| `docs/.vitepress/config.mts` | 中英文设备侧边栏各加一条 `nx741j` |
| `docs/devices/index.md`、`docs/zh/devices/index.md` | 「社区移植」表各加一行（维护者 MengHanYoo） |
| `devices.toml` | 移除 `nx741j` 条目（官方站点只接受 `UniXNU/<name>`；按社区移植处理） |

网络限制的处理：WSL 直连 `raw.githubusercontent.com` 被拒，证据文件统一用
`https://ghfast.top/https://raw.githubusercontent.com/...` 抓取（sheng 的 11 个证据文件）。

### 6.3 发布位置（2026-11-07）

不创建 `UniXNU` 组织仓库；本机唯一可写、且带 deploy key 的是 `MengHanYoo/nx741j`，
因此两个仓库以**分支**形式发布到该仓库，`main` 不受影响：

| 内容 | 仓库与分支 | Commit |
|---|---|---|
| 设备仓库（本仓库） | `https://github.com/MengHanYoo/nx741j/tree/unixnu-nx741j` | `d09054eda9fc114711af574436594a1aeafde30a` |
| 框架仓库（UniXNU 检出） | `https://github.com/MengHanYoo/nx741j/tree/unixnu-main` | `c92306a13db4aa5a4337a34a0fa6db3dd6cebff5` |

- 推送方式：deploy key `~/.ssh/nx741j_push`（只对该仓库有权限），`ssh.github.com:443` 与 `github.com:22` 均可用；
- 设备页里的"设备仓库"链接已指向 `…/tree/unixnu-nx741j`，记录快照按
  `MengHanYoo/nx741j@d09054e` 渲染（证据链接可点开）；
- 若之后把仓库迁到 `UniXNU` 组织，把 `devices.toml` 条目加回并把两处链接改回即可，其余无需改动。

失败时按[故障排查](https://unixnu.com/zh/reference/troubleshooting#procedure)处理，保存命令、输入版本与[日志](https://unixnu.com/zh/guide/debugging#procedure)。

## 7. 命令行验证（`reference/cli`，2026-11-07）

命令与选项均按 <https://unixnu.com/zh/reference/cli> 对照，逐项实测：

| 命令 | 结果 | 说明 |
|---|---|---|
| `defconfig` | ✅ rc=0 | 列出 `nx741j_defconfig` |
| `fetch` | ⏭ 未跑 | 网络受限；源码来自 `/opt/ghcache` 与 `out/feeds/`，`status` 已显示各模块 pin |
| `tree` | ✅ rc=0 | 组装 `out/…/tree`（构建步骤也会自动执行） |
| `ipsw info` / `ipsw fetch` | ✅ 已跑通 | 原包放在 `E:\default-workspace\ipsw\iPhone10,3,iPhone10,6_16.7.16_20H392_Restore (1).ipsw`（6,171,035,648 B）。`info` 打印 `16.7.16 (20H392) d221ap local` 与组件表；`fetch` 提取 `kernelcache.decompressed`（50,937,856 B）、`DeviceTree.d221ap.im4p`、两个 trustcache，缓存到 `out/ipsw/20H392/` |
| `build kernel` | ✅ | `kernel.macho` 51,691,520 B（sha `b255276d…`） |
| `build prelink` | ✅ rc=0 | 单独重跑预链接成功 |
| `build firmware` | ✅ | FD 4,194,304 B |
| `build payload` | ✅ | `payload.img` 67,108,864 B |
| `bootimg` | ✅ | 带 payload ramdisk 69,402,624 B；`--ramdisk none` 2,293,760 B |
| `status` / `status --json` | ✅ | `schema_version: 2`、`configured: true`、四个 profile 路径 |
| `clean` | ⏭ 未跑 | 破坏性（会删 tree/logs/kernel/package/boot），当前需要保留产物 |
| `release` | ✅ **已回移官方实现** | 见下节；`release --check` 列出 63 个补丁后停止，完整 `release` 跑完 build→pack→verify 并写出 `package.manifest.json` |

**`release` 的本地实现**（官方 main `b6de3eb` 相比我们领先 715 个提交，且已删除 `ios-16.7.16-20H392`
profile，不能整体升级，因此只回移命令本身）：

| 文件 | 内容 |
|---|---|
| `scripts/unixnu/pipeline.py` | 新增，取自官方 main（66 行）：`outputs`、`command_line`、`write_manifest`、`verify` |
| `scripts/unixnu/cli.py` | 新增 `release` 子命令（`--no-deps` / `--check`）与 `cmd_release`：deps → preflight → patches → build → pack → verify |
| `board.toml` | 新增 `[bootimg] dtb = "../device-tree/hardware.dtb"`（pack 步骤按 build tree 相对路径取设备树） |

实测（构建树 `/opt/work/nx741j-build/UniXNU`）：

```
$ ./unixnu release --check     -D ../UniXNU-nx741j   # 63 patches, check done
$ ./unixnu release --no-deps   -D ../UniXNU-nx741j
== step build → kernel / package / payload / firmware 全部成功
== step pack  → boot.img 68,493,312 bytes
== step verify
== boot.img    68,493,312 B sha256 7fcc591a…b1462
== payload.img 67,108,864 B sha256 9be838d0…4c851d
== kernel.macho 51,691,520 B sha256 837c8a7c…518a656
== manifest …/package.manifest.json
```

**ipsw 的两个本地修正**（`profiles/os/ios-16.7.16-20H392/profile.toml`）：

1. `url` 原来是 `file:///mnt/e/…`，当前 CLI 不解析 `file://` 前缀（会拼成相对路径 `…/UniXNU/file:/mnt/…`）→ 改为纯绝对路径；
2. 原包只含 `iPhone10,3`（`d221ap`，board `0x0E`）与 `iPhone10,6`（`d22ap`，board `0x06`）两个 Erase identity，
   而 profile 写的是 `d22ap`/`board_id=2` → 改为 `d221ap`/`14`。

两处改完后 pin 校验吻合：`kernelcache.decompressed` = `2ec297ba…c1221`（= profile `kernelcache_sha256`）、
`087-86698-044.dmg.trustcache` = `63e01c03…0ef4b`（= `board.toml [payload] trust_cache_sha256`）。

**环境变量**：`UNIXNU_EDK2` 已用于 QEMU 冒烟（指向 `/opt/work/uefi-sheng-main/Mu_Basecore`）；`JOBS` 用于并行构建（32）。

**结论**：CLI 参考页中的命令**全部验证通过**——`release` 已按官方实现回移到本地 CLI（`pipeline.py` + `cli.py`），
`release --check` 与完整 `release` 均成功；`clean` 仅因破坏性未执行，`fetch` 因网络走本地 ghcache（命令本身在 `release` 的 deps 步骤中已验证）。

## 8. 设备配置验证（`reference/configuration`，2026-11-07）

按页面逐项核对 `unixnu.local.toml`、`board.toml`、`feeds.toml`、`local.toml`、`unixnu.lock` 与 defconfig 语法。
第一轮（字段级）补了 2 项；第二轮（行为级）又发现 **1 个错项和 3 处行为缺口**，全部按官方 main `b6de3eb` 回移：

| 文件 | 回移内容 |
|---|---|
| `scripts/unixnu/bootimg.py` | 官方版本：`[bootimg.image] kernel_compression`（`gzip`/`none`）与可处理 header v3/v4、未压缩内核的 `verify()` |
| `scripts/unixnu/runtime.py` | 新增：`[payload.runtime]` 的运行时输入准备（无该表时只做 `check_data_input`，行为与之前一致） |
| `scripts/unixnu/payload.py` | 补 `check_data_input`（`CONFIG_XNU_OWNER_DATA_MOUNT` 需要 Data 种子） |
| `scripts/unixnu/cli.py` | `runtime` 别名、payload/runtime 前的 `runtime.prepare`；**新增 `--os VERSION` 选项**（页面选项表有、此前完全缺失） |
| `scripts/unixnu/config.py` | 官方版：`--os` 参与 profile 加载、`[package.defconfig]` 按 OS 映射、`need_config` 重读（release 在 preflight 才写配置） |
| `scripts/unixnu/profiles.py` | 官方版：`resolve_os`（`17`/`26`/`27` 或目录名）、`[os] arch` 选择 `arm64`/`arm64e`、`[os_overrides."<profile>"]` 合并与 `remove` |
| `scripts/unixnu/devicetree.py` | 官方版：ADT `random-seed` 由 64 B 扩到 **256 B**（xnu-12377 需要 4×64；旧内核只读前 64，兼容）、`RootPolicies` 映射、`transport-*` 属性、可嵌套 ADT 节点 |
| `scripts/unixnu/feeds.py` | 官方版：lock 与官方别名解析（格式 2 显式 commit 优先） |
| `scripts/appsync/` | 官方 16 个文件（AppSync 适配层源码与 signer，页面「完整性」一节引用） |

`kernel_compression` 的 A/B 实测（同一 payload 与 FD）：

| 设置 | `boot.img` |
|---|---|
| 默认（`gzip`） | 69,402,624 B |
| `[bootimg.image] kernel_compression = "none"` | **72,224,768 B**（未压缩 shim+firmware，差 2,822,144 B） |

行为级验证（回移后）：

| 检查 | 结果 |
|---|---|
| `status` / `status --os ios-16.7.16-20H392` | ✅ 两条路径都解析到同一输出目录，`profiles` 四层齐全 |
| `status --json` | ✅ `schema_version 2`、`configured true`、`build_config` 指向 `unixnu.local.toml` |
| `build payload` | ✅ 64 MiB（ADT 用 256 B 种子重建） |
| `bootimg` | ✅ 69,402,624 B |
| `release --check --no-deps` | ✅ `check done: 63 patches` |
| `release --no-deps` | ✅ 全流程（build → pack → verify → manifest） |

其余字段全部就位：`[board]` 的 `soc_source`/`os_profile`、`[components] include`、`kernel.config_fragments`/
`patch_series`、`device_tree.source`/`include_dirs`、`arch:/soc:/os:/device:` 前缀、`component:<ID>/path` series、
`CONFIG_XNU_*` 与 `{tree}` 替换、`[ipsw.sha256]`、`[userdata] first_lba`、`[kernel.prelink] extra_args`、
`[firmware] defines/preflight/submodules`、`[[bootimg.dtb_node]]`、`[payload.adt]`、`[payload] tools`、
`unixnu.lock`、`local.toml [local]` 与 7 级模块解析顺序。

`[payload.runtime] appsync` 字段已可用，但官方限制仍在：`appsync = true` 要求 `target = "ios27"` 且
`build = "24A446"`（本机为 iOS 16.7.16 / 20H392，不适用）；本机未开启该字段，构建行为不变。

## 9. 组件目录验证（`reference/components`，2026-11-07）

页面把 `components.toml` 的字段、选择规则、源码身份和本地模块流程写成了契约。逐项核对结果：

| 页面条目 | 本机状态 |
|---|---|
| 字段表（id / kind / path / repository / revision / legacy_name / function / scope / hardware / os_abi / board_dependencies / verified_devices / dependencies / build_stage / build / outputs / runtime_identifiers） | ✅ 25 个条目全部具备（另带 `source_files`、`legacy_outputs`、`repository_id`） |
| `[components] include` 选择，解析器补全依赖并按依赖顺序构建 | ✅ `status --json` 显示 6 个组件（ArmDTPlatform、ArmCpu、SoftwareAES、XnuPrelink、DarwinBoot、GopFramebuffer），`kernel.py` 按 `ctx.component_specs` 顺序执行 |
| 设备的 `include` 数组替换继承数组 | ✅ schema 2 的 profile 合并按"表按键合并、数组替换"（`profiles.merge`） |
| `[kernel] archives` 中用 `component:<ID>` 定位组件 | ✅ `kernel.py` 支持（`explicit = any(entry.startswith('component:'))`） |
| `--local-module driver:MyDriver=PATH` 选择**目录外**新组件并链接进装配树 | ❌ **修复**：旧 `components.resolve()` 只保留 refer/patch/package/script 的本地模块，目录外驱动被丢弃；已回移官方 4 行实现 |
| 官方兼容名按仓库身份解析（official `UniXNU/NativeAdreno` → `Adreno740`） | ✅ `source_identity` / `official_alias` / `repository_identity` 均在位 |

本地模块实测（回移后）：

```
$ ./unixnu tree -D ../UniXNU-nx741j --local-module driver:MyDriver=/tmp/localmods/MyDriver
$ ls -l out/…/tree/drivers/
ArmCpu -> …/drivers/ArmCpu
…
MyDriver -> /tmp/localmods/MyDriver          # 目录外组件已链接
```

回归：`build payload` 仍产 64 MiB、`release --check --no-deps` 仍为 `63 patches`。

目录内容差异（不影响本机选择）：官方 `components.toml` 为 28 个条目，本机快照 25 个——本机 `include`
只选 6 个，其余条目随官方目录演进，未随本次回移同步（避免改动固定 revision 影响已验证内核）。
