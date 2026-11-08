# 步骤 4 · 解析并检查构建配置

依据 <https://unixnu.com/zh/porting/steps> 与
<https://unixnu.com/zh/porting/device-repository> 的"解析并验证"一节。

## 1. 解析顺序（arch → soc → os → device）

| 层 | 来源 | 关键值 |
|---|---|---|
| ARM64 | `UniXNU/profiles/arch/arm64/profile.toml` | 63 条 XNU 补丁；`common.config`（DT platform、ARMCPU、timer、PAN、AES、corecrypto、pthread、trust-cache） |
| SoC | `UniXNU-sm8850-common`（本地覆盖） | `soc.toml` 固定实机 DTB `a5c109c0…`；`dts/sm8850.dtsi`；`configs/sm8850.config` 仅注释 |
| OS | `ios-16.7.16-20H392` profile | kernelcache `2ec297ba…`、slide `-0x1000000`、trust cache、`ios.config` |
| 设备 | 本仓库 | `[kernel] defconfig/config_fragments/patch_series/archives`、`[device_tree]`、`[firmware]`、`[payload]`、`[userdata]`、`[bootimg]` |

本地覆盖（`local.toml`）：`sm8850` → `../UniXNU-sm8850-common`；`ShengUfs` →
`/opt/work/work-local/ShengUfs-canoe`。`unixnu tree` 以符号链接进入组装树。

## 2. 组件与补丁

| 项 | 结果 |
|---|---|
| 组件 | 9 个：ArmDTPlatform/ArmCpu/SoftwareAES/XnuPrelink/DarwinBoot/GopFramebuffer/VirtioMmioBlock/VerifiedRoot/ShengUfs |
| 组件版本 | ArmDTPlatform `417ff95d`、ArmCpu `93a5030c`、SoftwareAES `4a45326d`、XnuPrelink `c645d76b`、DarwinBoot `3c757340`、GopFramebuffer `e47c1b8d`、VirtioMmioBlock `4e1013eb`、VerifiedRoot `974f043d`、ShengUfs `99d8bcd3` |
| 补丁顺序 | arch 63 条；soc/OS/device series 为空文件（文件存在，官方允许） |
| 归档 | `libShengUfs.a`（708,888 B，含 ABI 参考对象 + VerifiedRoot + 锚点）、`libArmCpu.a`（251,920 B） |

## 3. 配置去重（本轮修复）

`configs/nx741j_defconfig` 之前重复声明了 arch/OS 片段已提供的
`ARM_DT_PLATFORM`、`ARMCPU`、`CRYPTO_BOOTSTRAP`、`SOURCE_CORECRYPTO14`、`SOURCE_PTHREAD`，
导致 `.config` 出现重复行。现只保留设备差异项：

```
CONFIG_XNU_NATIVE_UFS=y
# CONFIG_XNU_NATIVE_ADRENO is not set
CONFIG_XNU_SOURCE_MOCKFS=n
```

验证：`CONFIG_XNU_ARM_DT_PLATFORM` 在 `.config` 中只出现一次（来自 arch 片段），
`NATIVE_UFS=y`、`NATIVE_ADRENO` 未设置、`SOURCE_MOCKFS=n`。

## 4. 引用文件存在性

`board.toml`、`porting.toml`、`feeds.toml`、`unixnu.lock`、`configs/nx741j_defconfig`、
`dts/nx741j.dts`、`dts/nx741j-hw.dtsi`、`patches/xnu/series`、`scripts/package.sh`、
`scripts/build-storage-bringup.py`、`package/Kit.toml`、`proprietary-files.txt`、
`local.toml.example` 全部存在；`[firmware]` 的 DSC 与
`Resources/Configs/nx741j.toml` 在 UEFI 树中可解析。

## 5. 构建结果（本轮实跑）

```sh
./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j   # rc 0
./unixnu fetch -D ../UniXNU-nx741j                        # rc 0
./unixnu status --json -D ../UniXNU-nx741j                # configured=true
./unixnu tree -D ../UniXNU-nx741j                         # rc 0
python3 scripts/porting-record.py check --device ../UniXNU-nx741j   # 14 areas
./unixnu kernel  → kernel.macho   sha256 4b7dd33ba4558d64b0ba1d264b3deb7cd3bec9a7edcfd19c1ac34a2060d0208c
./unixnu payload → payload.img    sha256 ca19871c5ba147cbcc2323958e7a30223d5020849298376131ec900a75dc0b4b
./unixnu bootimg → boot.img       sha256 c69bf61dc82b4adc1c232952d2d59f530e08c9c538c287debc1f8b75daf539f3
./unixnu build package            # rc 0，输出 out/.../package/Build/Out
```

## 6. 存疑

| 项 | 说明 |
|---|---|
| OS patch series | `os:patches/xnu/series` 为空；iOS 16 所需的 63 条补丁全部在 arch profile，设备层无补丁 |
| `[ipsw]` | 未配置，`unixnu ipsw` 不可用；用本地 kernelcache/trustcache 固定 |
| `[firmware].preflight` | 空；板级 overlay 手动执行 |
| 固件侧 V68 改动 | 在 UniXNU 框架检出内，尚未纳入设备仓库 |
| 运行资格 | `runtime_qualified/abi_qualified=false`，需实机启动 |
