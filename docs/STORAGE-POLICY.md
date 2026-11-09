# Verified-root 策略包（本地生成 + 主机验证）

存储路径的 verified-root 校验链有两半：`kits/VerifiedRoot/tools/manifest.py` 负责签名，
`kits/VerifiedRoot/src/verified_root.c` 负责逐块校验。设备仓库原来只锚定了空的
`verified_root_key` 和 `verified_root_policy_count = 0`（`package/storage-bringup/Stubs.c`），
所以 fail-closed。本页记录本轮结论：**策略包不需要外部输入，可以本地生成并验证**。

## 信封格式

`manifest.bin` = 160 字节头 + 384 字节签名 + N×32 字节块哈希（每 4 KiB 一块）。

| 偏移 | 长度 | 内容 |
|---|---|---|
| 0 | 8 | magic `VROOT01\0` |
| 8 | 4 | 版本 1 |
| 12 | 4 | 头长度 160 |
| 16 | 4 | 块大小 4096 |
| 20 | 4 | 保留 0 |
| 24 | 8 | 镜像字节数（LE） |
| 32 | 8 | 块数（必须等于 `ceil(image/4096)`） |
| 40 | 16 | volume UUID |
| 56 | 32 | 整镜像 SHA-256 |
| 88 | 32 | 哈希表 SHA-256 |
| 120 | 32 | 受信密钥（395 B）的 SHA-256 |
| 152 | 8 | generation（必须 ≥ 运行时最小值） |
| 160 | 384 | RSA-3072 / PKCS#1 v1.5 / SHA-256 签名（对头 160 B） |
| 544 | N×32 | 按序块哈希 |

受信密钥 395 B：`VROOTKEY`(8) + RSA-3072 模数(384) + `01 00 01`(3)，指数必须 65537。

## 生成

`scripts/mk-verified-root-policy.py` 包住 kit 的 `manifest.py`，管好 owner key（3072 位，
默认放输出目录、拒绝落进未忽略的工作区路径）并报告体积：

```sh
# 只看体积，不哈希 17 GiB
scripts/mk-verified-root-policy.py --image iosroot-GOLDEN.img --estimate

# 真正生成（第一次会生成 owner key）
scripts/mk-verified-root-policy.py --kit /opt/work/nx741j-build/UniXNU/kits/VerifiedRoot \
    --image root.img --volume-uuid 01e286f1-f4dd-4b2f-99d9-4973337fc788 --generation 1 \
    --output out/verified-root --private-key ~/.config/unixnu/nx741j-owner.pem
```

产物：

| 文件 | 用途 |
|---|---|
| `trusted-key.bin`（395 B） | 直接作为 `verified_root_key[]` 的初值 |
| `manifest.bin`（160+384+32N） | 策略信封，运行时放进 `verified_root_policies[]` 指向的内存 |
| `manifest.json` / `policy.json` | 体积、两个 SHA-256、密钥指纹、generation |

## 验证（主机，真实 C 实现）

用 gcc 编出 `libverifiedroot.so`（`src/verified_root.c` + `tests/openssl_provider.c`，链接 libcrypto），
再用 kit 的 `tools/verify_image.py` 跑真实 RSA 校验 + 全块校验：

```sh
gcc -std=c11 -Wall -Wextra -Werror -fPIC -shared -I $KIT/include \
    $KIT/src/verified_root.c $KIT/tests/openssl_provider.c -lcrypto -o libverifiedroot.so
python3 $KIT/tools/verify_image.py --library libverifiedroot.so --manifest manifest.bin \
    --trusted-key trusted-key.bin --image image.bin \
    --volume-uuid 01e286f1-f4dd-4b2f-99d9-4973337fc788 --minimum-generation 3 --output verify.json
```

| 用例 | 结果 |
|---|---|
| 4 块测试镜像（12,800 B）+ 3072 位 owner key | ✅ `PASS: real RSA signature and all 4 original blocks verified by C` |
| 改 1 个字节（偏移 5000） | ✅ 拒绝：`Actual image block authentication failed at index 1` |
| 运行时最小 generation 抬到 4 | ✅ 拒绝：`Real manifest authentication or authorized volume identity failed` |
| 生成器换成仓库脚本（`mk-verified-root-policy.py`，generation 3） | ✅ 同一套 C 校验通过，`signature_verified_by_openssl` 为真 |

## 体积（本机 17 GiB 根分区 dump）

| 项 | 值 |
|---|---|
| 镜像 | `iosroot-GOLDEN.img` 17,179,869,184 B |
| 块数 | 4,194,304 |
| 哈希表 | 134,217,728 B（**128 MiB**） |
| 信封 | 134,218,272 B |

128 MiB 远超 64 MiB 的 payload 预算，所以信封**不能随启动 payload 走**；要实现"根卷被验证"，
需要先定信封从哪里加载（候选：Data 分区的固定位置、或把镜像裁到真正要验证的范围）。
这一点是本轮之后剩下的设计问题，不是数据缺失。

## 编入内核（已验证）

`scripts/build-storage-bringup.py` 在 `build/verified-root/{trusted-key.bin,manifest.bin}` 存在时生成
`build/storage-bringup/Policy.c`（395 字节 owner key + 信封 + `verified_root_policies[16]`），用内核编译记录
编成 `Policy.o` 并 `llvm-ar r` 进 `build/sheng-ufs/libShengUfs.a`；`Stubs.c` 用 `-DVR_POLICY_BUILT_IN`
跳过空锚点定义。没有信封时行为不变（`count = 0`，fail-closed）。超过 `--max-policy-bytes`（默认 1 MiB）
的信封会被拒绝，提示改为运行时投递。

2026-11-09 在 E 盘构建树实测（8,320 B 测试镜像、640 B 信封）：

| 检查 | 结果 |
|---|---|
| 归档成员 | `verified_root.o`、`Stubs.o`、`Policy.o` |
| 符号 | `_verified_root_key` / `_verified_root_policies` / `_verified_root_policy_count` 均为 `D`（已定义） |
| 密钥字节 | `56524f4f 544b4559` = `VROOTKEY`，后接 384 字节模数 |

真实镜像还没编进去：17 GiB 需要 128 MiB 信封，会先被大小检查拦下，投递方式见下。

## 仍然缺的三件事（策略包之外）

1. **信封投递**：`Stubs.c` 里 `verified_root_policies[16]` 是空数组，谁来读信封、放在哪、多大，未定；
2. **xnu-8796 的 `source-storage-runtime.patch`**：现有补丁只到 xnu-12377，见 `STEP6-UFS.md`；
3. **bootargs 门控**：`native-source-storage=1`、`native-ufs-root=1` 未加，实机即便刷入也不会走存储路径。
