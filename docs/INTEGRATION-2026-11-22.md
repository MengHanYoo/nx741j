# NX741J 移植整合记录（2026-11-22）

本文件记录本轮仓库整合后的可复现状态。设备仓库可以单独克隆，再将
`patches/framework/UniXNU-ba9e14fb-nx741j.patch` 应用到固定版本的 UniXNU。

## 已完成

| 项 | 状态 | 证据 |
|---|---|---|
| 工作区和 WSL 移至 E 盘 | 完成 | C 盘旧树归档并逐文件 SHA-256 校验；WSL VHDX 位于 `E:\wsl\Ubuntu` |
| WSL 构建环境 | 通过 | 24 vCPU、约 19.5 GiB RAM、工具链、dtc、clang、QEMU 均由 `check-wsl-env.sh` 识别 |
| 4K 内核构建 | 通过 | `./unixnu kernel` 完成归档、链接和 prelink |
| prelink 私有指针闭包 | 通过 | 保留完整 Apple IOStorageFamily，避免 86 个私有函数/数据绑定被拆断 |
| APFS media content hint | 已生成候选 | 对 `instantiateMediaObject()` 的 ADRP/ADD 做 8 字节机器码固定校验，注入 `Apple_APFS` |
| trust cache | 可重建 | 原始 IM4P 自动转换为 1496 B bundle，SHA-256 `29dcf050…eefae4f` |
| QEMU 引导门禁 | 通过 | `UNIXNU: BootEntry`、`MARK #29`、`QEMU: EBS ok, entering the kernel` |
| XnuPrelink 单元测试 | 通过 | 67 tests OK |

## 本轮纠偏

最初方案是源码重建 IOStorageFamily并排除旧 bundle。实测该闭包需要解析
86 个跨组件私有函数/数据地址，其中多数不在本机内核导出中；继续执行会变成
整个 IOStorageFamily ABI 重实现。最终改为保留 Apple 原 bundle，仅补丁
`instantiateMediaObject()` 的唯一 content-hint 调用点。

prelink 增加通用 `text_literals` 配置：

- 固定原始 8 字节指令；
- 只接受相邻 ADRP/ADD；
- 校验原目标可映射、新目标在 ±4 GiB 范围；
- 只允许补丁点本身变化，其余可执行字节必须逐字节一致；
- patch 与字符串地址写入 prelink JSON 报告。

Libm 中 9 个看似“跨组件 PC-relative 指令”的地址已核实为 1224 B 常量池，
使用完整 SHA-256 指纹排除，而不是放宽代码检查。

## 构建命令

```sh
git clone git@github.com:MengHanYoo/nx741j.git UniXNU-nx741j
git clone https://github.com/UniXNU/UniXNU.git
git -C UniXNU checkout ba9e14fb46cff0f2d3e8b1a006bd9f849cbd5ce6
./UniXNU-nx741j/scripts/apply-framework-patch.sh UniXNU

cd UniXNU
./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j
./unixnu tree -D ../UniXNU-nx741j
./unixnu kernel -D ../UniXNU-nx741j -j 16
./unixnu payload -D ../UniXNU-nx741j
./unixnu firmware -D ../UniXNU-nx741j
```

`unixnu.local.toml` 是本机输入文件，已被上游 `.gitignore` 排除。至少需要
指向本机 kernelcache、UEFI 树和 `build/trustcache.bundle`；完整示例见
`docs/LOCAL-DEVELOPMENT.md` 与本文件的路径表。

## 未完成 / 边界

- QEMU 已完成引导链门禁，但未在 QEMU 中验证 APFS 根卷与 `launchd`。
- smoke 的六个场景曾完整通过；本轮最后总门禁遇到 AAVMF/TCG 单场偶发卡死。
  按用户决定暂停继续修 smoke，不把最新总门禁宣称全绿。
- `root_match_volume_uuid` 必须从真机 APFS 超级块读取，当前未填。
- IOStorageFamily 的 `Apple_APFS` 是 bring-up 定点补丁，需真机验证后再决定是否
  替换为完整的 provider property 读取实现。
- 存储持久化/性能、显示与亮度、Synaptics TouchComm、PMIC/TLMM 按键、Adreno
  840v2、USB、ADSP/音频、电池/温控、WCN7880、基带、相机、背屏仍未完成。
- 真机验证与刷机未在本轮执行。

## 机器码补丁定位

```text
bundle:  com.apple.iokit.IOStorageFamily
section: __TEXT_EXEC
offset:  0x55f0
expect:  a7bfff90e7ec1691
literal: Apple_APFS
```

该地址属于旧 `IOBlockStorageDriver::instantiateMediaObject()`，原始目标为
空字符串加载。补丁后的目标字符串存放在 `__PRELINK_INFO` 尾部，并记录在
`kernel.json` 的 `text_literal_patches`。
