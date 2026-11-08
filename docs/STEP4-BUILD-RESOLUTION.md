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
./unixnu kernel  → kernel.macho   sha256 512157c10bc1c63b81c1d88106e411a82fc071c55f80c2a46b8609222599ebbe
./unixnu payload → payload.img    sha256 fc12bdfaece7e8415fd059277a6d1e3445b005a0897a7644126876882ed54124
./unixnu bootimg → boot.img       sha256 a480d7d8ec56ff76ea0f6047c9929c3795a37ca03985c6fd0f696bbe5d1ca206
./unixnu build package            # rc 0，输出 out/.../package/Build/Out
```

## 6. 存疑

| 项 | 说明 |
|---|---|
| OS patch series | `os:patches/xnu/series` 为空；iOS 16 所需的 63 条补丁全部在 arch profile，设备层无补丁 |
| `[ipsw]` | ✅ 已配置：Apple CDN URL、iPhone10,3/16.7.16/20H392、`d221ap`、chip `0x8015`、board 14；成员 sha256 已按本机 IPSW 实测（kernelcache `2d6a13d0…`、DeviceTree `889e8383…`、两个 trustcache `9cc478fe…`/`63e01c03…`）；`unixnu ipsw info` 验证通过 |
| `[firmware].preflight` | ✅ 已生效：`kits/nx741j/firmware-preflight.py` 自动应用 overlay + vendored `UniXNUDxe.c`；`unixnu build firmware` rc 0（FD sha256 `f931527c…`，keys 驱动已移除） |
| 固件侧 V68 改动 | ✅ 已入仓库：`package/upstream/UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.c`（直启兜底）+ overlay 中的 keys-disabled DSC/FDF/INC |
| SoC pin | ✅ 已对齐：`feeds.toml` pin `5ffa7619c0ffd968e3b7295b4c65bb8505c0b992`，构建树克隆 reset 到同一提交（GitHub 私有仓库仍需你推送，但本机构建与 pin 一致） |
| 运行资格 | `runtime_qualified/abi_qualified=false`，需实机启动 |

## 7. 本地流水线（替代 GitHub Actions）

```sh
scripts/ci-local.sh            # 全流程：defconfig → fetch → status → tree → 记录检查
                               #          → kernel → payload → bootimg → package → firmware
scripts/ci-local.sh --check    # 仅解析、组装与记录检查
UNIXNU=/path/to/UniXNU scripts/ci-local.sh   # 指定 UniXNU 检出
```

本轮完整跑通（rc 0）：kernel `512157c1…`、payload `fc12bdfa…`、boot `a480d7d8…`、
FD `15e12f2c…`；`scripts/prelink-status.py` 输出 `runtime_qualified=false`（需实机启动，
工具在硬件证据出现前保持 false，不允许伪造）。
