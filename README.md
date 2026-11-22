# UniXNU-nx741j

**English** | [简体中文](README.zh-CN.md)

Device repository for the **Nubia Z80 Ultra (NX741J)** — Qualcomm **SM8850** (Kaanapali,
codename `canoe`), 8 cores, Adreno 840, UFS, panel `bf375`/`rm692h5` 1440×3200.

The device-proven NX741J iOS route is:

```
bootloader -> stock efisp LinuxLoader -> boot_a shim loader
  -> SWEETXNU payload -> XNU -> HFSX ufsroot0 -> launchd
```

The repository also retains the official Mu/UEFI -> DarwinBoot implementation
and its diagnostics. It is not the final device image: Mu stops at DxeCore/BDS
handle enumeration on this unit. See
[PROVEN-ROUTE-2026-11-23.md](docs/PROVEN-ROUTE-2026-11-23.md) for the installed
route and exact hashes.

Target: **iOS 16.7.16 (20H392)**, XNU **8796.141.3**, arm64, 4 KiB pages. The OS profile is pinned
in [board.toml](board.toml); it is the only IPSW available for this port.

## Status

| Stage | Scope | State |
|---|---|---|
| 1 | board description, components, build inputs | done |
| 2 | firmware (UniXNU UEFI app, DarwinBoot) | done |
| 3 | payload (kernel, ADT, trust cache) | done |
| 4 | boot chain handoffs | proven loader -> SWEETXNU -> XNU route installed and read back |
| 5 | kernel bring-up | proven kernel reaches interrupt setup in the loader QEMU model and was previously device-proven to userspace |
| 6 | storage and drivers | fixed 39-extent HFSX `ufsroot0`; display/touch/HID/CPU paths in the proven kernel |
| 7 | USB/Wi-Fi/audio/baseband/camera/back display | frozen by owner until iOS userspace is available |

**2026-11-23 checkpoint.** The integrated route uses the original IPSW root,
not `iosroot-GOLDEN.img`. A 13-block root repair, stock efisp and a 54,329,344
byte `boot_a` were written in one 9008 session and every region matched on
readback. After reset, the previous QUSB_BULK failure did not reappear. See
[PROVEN-ROUTE-2026-11-23.md](docs/PROVEN-ROUTE-2026-11-23.md).

**Stage 5 milestone (2026-11-12).** The kernel console now renders on the panel, so every boot stage
is observable on the device instead of a colour-bar screen:

```
Registered class IOResources.
...
Starting from prelinked kernel.
kext com.apple.kpi.bsd Loaded.
...
loading dependencies com.apple.kpi.mach
```

**Stage 6 (UFS).** The storage path now runs on the firmware state the bootloader leaves behind
(official porting/ufs condition 2): the request list is adopted at the firmware UTRLBA, stream 0x60
is forced to match and bypass (`SMR[0]=0x80000060`, `S2CR[0]=1`), and the vendor cold-init
sequence (PHY calibration, SERDES start, PCS ready, HCE, DME link startup) runs behind
`native-ufs-cold-init=1`.  Every wait is a bounded busy-wait, because `IODelay` blocks on the
timer on this build.

| Condition | State | Evidence |
|---|---|---|
| 1 registers | done | `CAP=0xd5c7031f`, `HCS=0x10f`, `HCE=1` |
| 2 DMA / queue ownership | done | list installed and started (`rsr=1`); the controller fetches only the firmware queue region |
| 3 command completion | done | `TEST UNIT READY lun0: ocs=0 resp=00 scsi=00 db=0` |
| 4 known-block read | done | `READ(10)` LBA 1 returns `sig=4546492050415254` (EFI PART), `entlba=2 cnt=32 esz=128`; entries parsed, `userdata` at `first=0x4a6328` |
| 5 root volume | partial | official `SHNGROOT` manifest read through the kernel transport, `img=0x1f3800000`, `w0=0x14fe08600`, `w1=0xa3800000`, `bounds=1 ord=1`; OS window block 4 returns `NXSB`; publishing the windows as `IOMedia` is next |
| 6 persistence, 7 performance | deferred until iOS starts (owner decision) | |

The official `./unixnu userdata` image (7.8 GiB, `SHNGROOT` v1, OS 5,635,081,728 B at
`0x100000`, SystemOS 2,743,074,816 B at `0x150000000`) is flashed to `userdata` at LBA 4,875,048
and reads back byte-identical.  `nxRootRead` issues one 4 KiB `READ(10)` per command through the
adopted queue, so the block device and the root probes read real filesystem data.  data-a-live
`pinfile` also works here (pinned 64 MiB and 16 GiB files, 1 and 11 extents, `rawtest`/`rawcheck`
through `/dev/block/sda12`), i.e. the dm-default-key layer can be bypassed as on the reference
device.  The kernel window covers the firmware queue: DarwinBoot prefers a free run containing
`0xC3B00000`, falls back for QEMU, and reports up to `0xD0000000`.

## Session log — 2026-11-12

### Kernel console log channel

The panel shows the kernel log but cannot be scrolled or copied, and this board has no UART, so
the console is mirrored into DRAM and collected on the next boot:

| Layer | What it does |
|---|---|
| `patches/xnu/0021-console-log-capture.patch` | `osfmk/console/serial_general.c` appends every `console_printbuf_putc` byte to a 128 KiB buffer; the `NXLOG1` magic is written on the first character, because the buffer lives in `__BSS` and a static initialiser is never copied into the window |
| `loader/p7.c` | scans DRAM for the magic, writes a status record (`NXLGST1` + found PA + length + 64 bytes) to `rawdump` LBA `4764464` and the log to `4764465`; log writes go through a 4 KiB aligned bounce buffer because `BlockIo` rejects the unaligned `found+12` source |
| `wsl/flash-boota-9008.ps1 -ReadTo` | reads any partition back over 9008 (`-ReadSectors`), the only way to look at those records |

Constraint: the buffer only reaches DRAM on the boot that produced it, and entering 9008 re-initialises
DDR and wipes it. The capture therefore needs one warm reset after the kernel has printed — which is
exactly what the boot-menu keys are needed for.

### Boot-menu keys

The device tree puts the keys on TLMM GPIOs (`gpio_keys`: VOL+ 101, SHOOT 95, FOCUS 179, flags `1` =
active low). Three driver variants were tried from DXE, and every one that touches the TLMM window
makes the firmware assert immediately after `Nx741jKeysDxe` is loaded, ending in ZTE memory-dump mode:

| Variant | Result |
|---|---|
| GCD descriptor check + `AddMemorySpace` + pin config writes | assert |
| `AddMemorySpace` + pin config writes | assert |
| plain read of `GPIO_IN_OUT` | assert |

`Nx741jKeysDxe` therefore publishes its `SimpleTextInputEx` device with the TLMM path disabled: the
menu renders and the `Connect ConIn failed` message is gone, but keys are inert. The documented
OS-side path (`ShengHidInput` `ButtonProbe`, `buttons=1`, report 2 bits for Power / Volume) is the
next step; the PMIC keypad (`KeypadDxe` + a ZTE `KeypadDeviceLib`) is the firmware fallback.

### Real partition layout (vendor GPT backup, 4096-byte logical blocks)

| LUN | Partition | First LBA | Last LBA | Size |
|---|---|---|---|---|
| 0 | `persist` | 8 | 8199 | 32 MiB |
| 0 | `misc` | 8200 | 8455 | 1 MiB |
| 0 | `metadata` | 4727592 | 4743975 | 64 MiB |
| 0 | `rawdump` | 4743976 | 4875047 | 512 MiB |
| 0 | `userdata` | 4875048 | 4875047 | 0 in this backup |
| 4 | `boot_a` | 119078 | 143653 | 96 MiB |
| 4 | `efisp` | 748894 | 749661 | 3 MiB |

`rawdump` is empty in the vendor image, which is why the payload (offset 0) and the firmware FD
(+16384 sectors) can live there without touching Android. The `userdata` entry in this backup has zero
length, so the live GPT has to be read again before stage 5 uses it.

### UFS facts from the shipped device tree (`dts/nx741j-adt.dts`)

| Property | Value | Used by |
|---|---|---|
| `reg` `ufs_mem` | `0x1d84000` / `0x3000` | `NativeUfs` host window |
| `reg` `ice` | `0x1d88000` / `0x18000` | inline crypto engine |
| `reg` `mcq_sqd` / `mcq_vs` | `0x1da5000`/`0x2000`, `0x1da4000`/`0x10` | MCQ (unused; MCA is derived at `0x1da0000`) |
| PHY | `ufsphy_mem@1d80000`, `qcom,ufs-phy-qmp-v4-canoe` | `package/storage-bringup/ufs-cold-init.c` (`0x1d80000`) |
| `iommus` | `apps_smmu 0x60` | bypass in `SmmuTakeover.h` uses stream `0x60` |
| `interrupts` | SPI 265, level high | not wired; the driver polls |
| `dma-coherent`, `qcom,iommu-dma = "fastmap"` | present | queues may be cached; identity IOVA |
| `power-domains` | host `<gcc 3>`, PHY `<gcc 2>` | not enabled explicitly |
| `reset-gpios` | TLMM `0xd9` (217), active low | not used |
| `lanes-per-direction` | 2 | — |
## Building

Two entry points, both run on Linux/WSL:

```sh
# Fast iteration: kernel -> payload -> firmware, no QEMU smoke.
./scripts/build-fast.sh ~/nx741j-build/UniXNU

# Milestone gate: the same three steps plus a QEMU boot-chain run.
# Use it before flashing anything that touches the firmware memory layout.
sudo ./scripts/build-and-test.sh ~/nx741j-build/UniXNU
```

The gate asserts three markers — `UNIXNU: BootEntry`, `MARK #29`, `QEMU: EBS ok, entering the
kernel` — and fails the build when any is missing.

## Flashing

The vendor ABL on this device rejects `fastboot boot`, `fastboot flash` and `fastboot erase`
(`unknown command`), so everything goes through **9008** and the EFI system partition:

| Region | LUN | LBA | Size |
|---|---|---|---|
| `efisp` (our loader) | 4 | 748894 | 3 MiB |
| `rawdump` — firmware FD | 0 | 4760360 | 4 MiB |
| `rawdump` — payload | 0 | 4743976 | 64 MiB |

```powershell
wsl\flash-boota-9008.ps1 -Img <image> -StartSector <lba> -Label <label> -Lun <lun> -PartitionBytes <bytes>

# read back instead of writing (log channel, register dumps, verification)
wsl\flash-boota-9008.ps1 -Img <any> -ReadTo _tmp\dump.bin -StartSector <lba> -ReadSectors <n> `
    -Label rawdump -Lun 0
```

Any Android boot overwrites `efisp`, so it has to be re-flashed before every payload test. Writing
back the stock `efisp` (`sha256 4df18359...`) restores the vendor boot chain.

## Repository layout

| Path | Contents |
|---|---|
| `board.toml` | board description: components, kernel archives, prelink roots, boot args |
| `dts/` | ADT source for the payload |
| `loader/` | efisp EFI loader (`p5.c` ... `p7.c`) and its images |
| `package/firmware-overlay/` | board-level firmware code, applied into the UEFI tree |
| `patches/xnu/` | device XNU patch series (see `series` for the applied order) |
| `patches/firmware/` | firmware patches (DSC/FDF, platform drivers) |
| `patches/drivers/` | device driver diffs (adopt-transport root work) |
| `scripts/` | build helpers (`build-fast.sh`, `build-and-test.sh`, ...) |
| `scripts/patch-native-ufs-*.py` | build hooks for the ShengUfs prelude and the driver diff |
| `docs/` | analysis, audits, per-stage notes, hardware inventory |

## Recent fixes worth knowing

| Patch / change | Why |
|---|---|
| `0010-console-acquire-screen.patch` | `PE_create_console` uses `kPEAcquireScreen`, so `kprintf` reaches the panel |
| `0015-iodevicetree-null-interrupt-parent.patch` | `IODTMapOneInterrupt` fed a NULL parent into `IODTGetICellCounts` and panicked during `InitIOKit` |
| `component:GopFramebuffer` in `[kernel] archives` | the display component was selected but never linked |
| `patches/drivers/native-ufs-adopt-root.patch` | adopt-transport block reader, manifest layout, `startRoot` preference, `probe=12` reporting |
| bounded busy-waits | `IODelay` blocks on the timer on this build, so every UFS wait spins with a deadline |
| `native-ufs-cold-init=1` | vendor-derived PHY calibration + link startup before any command |
| `patches/xnu/0021-console-log-capture.patch` | console bytes are mirrored into a DRAM buffer the efisp loader can find on the next boot |
| `scripts/patch-native-ufs-adopt-fix.py` | the adopt path cleaned 32 KiB out of a 16 KiB mapping, which faulted in `adoptAndRead+0x844` |
| `patch-native-ufs-cold-init.py` (unconditional) | the cold-init hook was only called for `probe!=0`, so it never ran with the takeover settings |
| `wsl/flash-boota-9008.ps1 -ReadTo` | 9008 read-back, used for the log channel and for register dumps |

## Documentation

- [docs/PROVEN-ROUTE-2026-11-23.md](docs/PROVEN-ROUTE-2026-11-23.md) — current device-proven route, hashes, root repair, QEMU and 9008 readback evidence
- [docs/TASKS.md](docs/TASKS.md) — consolidated task and status log
- [docs/INTEGRATION-2026-11-22.md](docs/INTEGRATION-2026-11-22.md) — reproducible framework patch and integration checkpoint
- [docs/SWEET-ROOT-2026-11-22.md](docs/SWEET-ROOT-2026-11-22.md) — current data-a-live extent/HFSX root, built artifacts and device gate
- [docs/STATUS-2026-11-22.md](docs/STATUS-2026-11-22.md) — current acceptance boundary and remaining work
- [docs/DEVICE-TEST-2026-11-22.md](docs/DEVICE-TEST-2026-11-22.md) — real-device 9008 write and reset record
- [docs/STATUS-2026-11-11.md](docs/STATUS-2026-11-11.md) — stage-by-stage status
- [docs/BOOT-CHAIN.md](docs/BOOT-CHAIN.md) — boot chain against the official description
- [docs/UFS-QCOM-PHY-PORT.md](docs/UFS-QCOM-PHY-PORT.md) — restored PHY tables and call order
- [docs/HARDWARE-INVENTORY.md](docs/HARDWARE-INVENTORY.md) — hardware facts with sources

## Licence

Patches and board code follow the upstream UniXNU licensing (BSD-2-Clause-Patent where noted in the
individual files). Vendor-derived tables are data recovered from the device's own firmware.

## Current status — 2026-11-23

The current device route does not use APFS VerifiedRoot or the adopted NativeUfs queue. It exposes
the pinned 16 GiB HFSX root through the data-a-live-style `ufsroot0` path. The patched root adds the
MobileGestalt container and launchd/SpringBoard bring-up preferences missing from the plain IPSW
root. The integrated image passed the static gate and the shim-loader QEMU gate; 9008 install and
readback are complete. Direct launchd/lockscreen capture remains open while step 7 is frozen.

The default payload is now the normal root path:

```text
native-source-storage=1 native-ufs-root=1 sheng-pmgr=2
native-ufs-cold-init=1 native-ufs-keepqueue=2
```

The PMGR ABI builder passes its 340-entry virtual-table audit and the final native kernel contains
`_sheng_pmgr_run`; its report still says `runtime_qualified: false`. Kernel, root payload and UEFI
firmware build successfully. QEMU reaches `BootEntry`, `MARK #29` and
`EBS ok, entering the kernel`.

Final host hashes:

| File | SHA-256 |
|---|---|
| `kernel.macho` | `255cdf323d1d00bc5e077a628d9f6687c6f354de7dc2d9739f2923a70dbd3c9b` |
| `payload.img` | `8801fbd728c35227c193d2aac8dfcf1302ea02a5b2f47c55b07c19f9ab0de95c` |
| `payload-probe12.img` | `fa777aeb86eab4b60c573b3086d2b309ffeed1972f00b4a91e18c34337cf0036` |
| `SILICIUM_UEFI.fd` | `3ce5c6d2c61891975c52846358bff3f3d9744413d61fd732ea48f7507a49a009` |
| `boot.img` | `dbb6c35c9e133c35fe3b7c2d52a4b129239db68ca3d3018309acce7aa10e004d` |

The phone was later connected through 9008: the root image, `NXROOTP1` anchor, payload, FD and
the then-current `efisp-p7.img` were all written and read back. A later EDL read recovered the
kernel's captured log: XNU completed early VM and kext collection, then reached
`NATIVE_UFS start` and `NATIVE_UFS bringup: enter`. After reset the phone still enumerated as
`QUSB_BULK_CID:046A_SN:098F0FC3`; this is not an iOS success. The old loader also wrote its
progress record to `super` LBA 20224 because `gRawBase` was never initialized. The source now
binds the rawdump base explicitly, enables the UFS snapshot, and builds `efisp-p7.img` with
SHA-256 `73b56bf2b2192706ec724974f832457d5b95ab1382038e3442bc392127b4b246`. Recovery and the
single-sector check are documented in
[docs/DEVICE-TEST-2026-11-22.md](docs/DEVICE-TEST-2026-11-22.md). Step 7 and persistent
Data/performance work stay deferred until iOS starts. The aggregate smoke suite remains separate
from the green direct QEMU gate.

The repaired loader and final probe payload
`fa777aeb86eab4b60c573b3086d2b309ffeed1972f00b4a91e18c34337cf0036` were written to the phone
and read back at 13:32. The checked `super` sector already matched the 2026-11-09 backup, so no
repair write was needed. The next hardware step is another 9008 entry to read the new progress,
NXLOG and UFS snapshot records.

The complete UniXNU-side delta is in `patches/framework/UniXNU-ba9e14fb-nx741j.patch`; a clean
no-hardlinks clone passes apply and reverse checks. See
[docs/INTEGRATION-2026-11-22.md](docs/INTEGRATION-2026-11-22.md) and
[docs/STATUS-2026-11-22.md](docs/STATUS-2026-11-22.md).
