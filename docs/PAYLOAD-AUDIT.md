# 负载（concepts/payload）对照分析

依据 `https://unixnu.com/zh/concepts/payload`；实测对象 `out/.../payload/payload.img` 与 `payload.json`。

## 1. 卷布局

| 官方路径 | 我们 | 大小 |
|---|---|---|
| `/EFI/UniXNU/unixnu.toml` | ✅ | 623 B |
| `/EFI/UniXNU/Payloads/kernel.macho` | ✅ | 51,859,456 B |
| `/EFI/UniXNU/Payloads/kernel.macho.adt` | ✅ | 13,228 B |
| `/EFI/UniXNU/Payloads/kernel.macho.trustcache` | ✅ | 1,496 B |
| `/EFI/UniXNU/Payloads/scratch-data.img` | ❌ 无（没有 Data 种子） | — |

卷 64 MiB，已用 51,874,180 B（**81%**），剩 14,181,888 B；`payload.py` 在超过 95% 时会拒绝构建。

## 2. 配置

官方示例是 `[boot] timeout=3` + `[kits] autoload=false` + 一个 `[[entry]]`（有 Data 时带 `initrd`）。
我们是：`timeout=5`、`rotate/fallback_timeout/connect_all`、`[ui]` 六项、`[[entry]]` ×4（xnu + reset/shutdown/tools），
**没有 `initrd`**（与无 Data 种子一致），`kits autoload=false`。字段都在官方 schema 内。

## 3. ADT sidecar

官方要求（格式 2）：按 `[device_tree] source` 编译，共享生成器结合硬件 DTB 与 payload 输入；
且 ADT 必须为每个输入预留占位符：`/chosen/memory-map/{RAMDisk,PackedRAMDisk,TrustCache}`、`/chosen/darwinboot-memory-size`；
缺占位符时 DarwinBoot 返回 `EFI_COMPROMISED_DATA`。

| 占位符 | 我们 |
|---|---|
| `memory-map/TrustCache` | ✅（16 B，地址/大小由 DarwinBoot 填） |
| `memory-map/RAMDisk` / `PackedRAMDisk` | 无——**没有 Data 输入，不需要**（真机 MARK #29 已验证通过） |
| `chosen/darwinboot-memory-size` | ✅ `0x10000000`（256 MiB） |

## 4. 内核参数

官方规则：字符集 `[A-Za-z0-9_.\-]+`、可选 `=value`、合并后 ≤1000 字节。
我们：`keepsyms=1 kextlog=0xffff native-ufs-probe=14 native-ufs-diagnostics=5 native-ufs-dma-offset=0 -v serial=3`，
**106 字节**、字符全部合法（`native-ufs-*` 是板级探针参数）。

## 5. trust cache

官方校验：计数 1–128、每 cache 一个偏移项、以版本 2 开头、每项 24 字节、哈希类型 1–4、按升序。
我们的包 1,496 B，由 `scripts/mk-trustcache-bundle.py` 生成，`[payload] trust_cache_sha256` 固定 `29dcf050…`；
真机 DarwinBoot 已通过（MARK #29）→ 格式符合。**偏离**：来源是自制扁平包，不是 IPSW 的 IM4P 原包（安全记录里已列入 bring-up 临时策略）。

## 6. Data ramdisk

官方两种形式：dense（`RAMDisk`，整镜像复制）与 packed（`PackedRAMDisk`，`DBRDISK1` extent 表）；
`seed-data-grow.py` 需 macOS。
我们**没有 Data 种子** → 无 Data 卷；这正是阶段 7（用户态 Data 挂载）缺的输入。阶段 6 的根卷走 SHNGROOT，不依赖它。

## 7. 确定性

官方：`mformat`/`mcopy` 固定 1980-01-01 时间戳；ADT 含随机种子，所以两次负载构建不同；报告记录哈希。
我们：`mdir` 显示时间戳均为 1980-01-01 ✅；`payload.json` 记录 kernel/adt/trust_cache/payload 四个 sha256 ✅；
`data_ramdisk_sha256` 与 `gpu_addon_sha256` 为 null（无对应输入，符合预期）。

## 8. 交付方式差异（重要）

官方把负载当作 **Android boot.img 的 ramdisk**，`./unixnu bootimg` 打包、`fastboot boot` 启动。
本机 ABL 拒绝第三方 boot.img（`docs/BOOT-FIRST.md`），所以实际路径是：

```
payload.img --(9008)--> rawdump+0 --(efisp loader)--> 内存 --(FDT /chosen/linux,initrd-*)
        --(Mu-Silicium + UniXNURamDiskDxe)--> 块设备 --(FAT)--> UniXNU/DarwinBoot
```

即**内容与格式完全一致，交付通道不同**；`./unixnu bootimg` 仍会生成 boot.img（QEMU 与备用路径用），只是本机不刷它。

## 9. 还差什么

1. **Data 种子**（阶段 7 需要）：本机是 Android F2FS，需从设备取 Data 分区种子，或找 WSL 可跑的 `seed-data-grow.py` 替代（原脚本 macOS-only）；
2. **容量余量**：81% 已用，内核再涨 ~10% 会触发 95% 上限 → 提高 `[payload] size_mib`（rawdump 区 512 MiB，足够）；
3. 在负载文档引用处注明「本机不走 boot.img ramdisk」这一交付差异。
