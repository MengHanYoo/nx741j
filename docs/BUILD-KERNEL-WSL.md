# 本地 WSL 编译 XNU（以实机 DTB 为准）

按官方 [构建与启动](/zh/porting/building) 与 [步骤 7](/zh/porting/steps) 的顺序，在本机 WSL 里从
**实机导出的 FDT** 出发编译 XNU 内核。

## 设备树链路（三段，别混淆）

```
实机 FDT（9008/系统导出，stock/dt/live_fdt.dtb）
   │  scripts/audit-live-dtb.py：把本仓库声称描述的 10 个节点与实机逐个比对
   ▼
dts/nx741j.dts + UniXNU-sm8850-common/dts（SoC）
   │  dtc（unixnu kernel/payload 内部调用）
   ▼
out/.../device-tree/hardware.dtb      ← 板级 DTB（进 ADT）
   │  make-adt（payload 步骤）
   ▼
payload 内 EFI/UniXNU/Payloads/kernel.macho.adt   ← XNU 读的 ADT
```

固件自己用的 DTB 是 FDF 内嵌的 `FdtBlob`（`DtPlatformDxe` → `gFdtTableGuid`），与上面这条链**互不影响**；
efisp loader 传的 `x0=FDT` 只是保留 BootShim 约定。

## 命令（ext4 构建树，16 线程）

```sh
cd ~/nx741j-build/UniXNU
R=/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j

# 0) 用实机 FDT 审计板级 DTS（10 个节点逐个比 compatible/reg/interrupts/status）
cd "$R" && python3 scripts/audit-live-dtb.py \
    --live /mnt/e/default-workspace/z80u-collect/stock/dt/live_fdt.dtb \
    --include /mnt/e/default-workspace/z80u-collect/repos/UniXNU-sm8850-common/dts

# 1) 内核（这里做一次真正的全量：删掉对象树）
cd ~/nx741j-build/UniXNU
rm -rf out/nx741j/ios-16.7.16-20H392/nx741j/tree/build/xnu/ios-obj \
       out/nx741j/ios-16.7.16-20H392/nx741j/tree/build/xnu/ios-sym
./unixnu kernel  -D "$R" -j 16

# 2) payload（由同一份 DTS 生成 ADT）与启动镜像
./unixnu payload -D "$R" -j 16
./unixnu bootimg -D "$R"
```

## 2026-11-09 本次结果

| 步骤 | 结果 |
|---|---|
| 实机 FDT 审计 | **10 claimed nodes, 0 differences** |
| 全量内核编译（删对象树，16 线程） | 成功，约 3.5 分钟（有并行任务时） |
| `kernel.macho` | 51,789,824 B，sha256 `4db46ca92e32f4a4…` |
| `hardware.dtb` | 56,674 B，sha256 `138cd8ea41aa075b…` |
| `payload.img` | 64 MiB，sha256 `e6f09f6c6695d7f8…` |
| `boot.img` | 68,677,632 B，sha256 `136d119a9f6ad342…` |

编译产物里的额外对象（本仓库贡献）：`ArmSmmu4K*.o` + `SmmuBind.o`（SMMU 绑定）、`verified_root.o` + `xnu_crypto.o` +
`Stubs.o`（VerifiedRoot）、`NativeUfs.o`（ShengUfs）。

## 校验

| 检查 | 命令 | 结果 |
|---|---|---|
| 启动级断言 | `python3 scripts/check-bringup.py --tree <out>/tree` | 20/20 |
| 实机 DTB 一致性 | `scripts/audit-live-dtb.py --live stock/dt/live_fdt.dtb` | 10/10 |
| 适配记录 | `python3 scripts/porting-record.py check --device <repo>` | 14 areas |

## 与官方文档的对应

| 官方步骤 | 本页命令 |
|---|---|
| 步骤 5 准备硬件描述 | 第 0 步（审计）+ 构建里的 `hardware.dtb` / ADT |
| 步骤 7 构建并启动 | 第 1–2 步（kernel → payload → bootimg） |
| 启动链第 4 次交接 | `kernel.macho` + `kernel.macho.adt` 由 DarwinBoot 读入窗口 |
