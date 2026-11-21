# UniXNU-nx741j

**English** | [简体中文](README.zh-CN.md)

Device repository for the **Nubia Z80 Ultra (NX741J)** — Qualcomm **SM8850** (Kaanapali,
codename `canoe`), 8 cores, Adreno 840, UFS, panel `bf375`/`rm692h5` 1440×3200.

The port boots Apple's XNU through the official UniXNU chain:

```
bootloader -> efisp loader -> UniXNU (UEFI) -> DarwinBoot -> XNU -> userland
```

Target: **iOS 16.7.16 (20H392)**, XNU **8796.141.3**, arm64, 4 KiB pages. The OS profile is pinned
in [board.toml](board.toml); it is the only IPSW available for this port.

## Status

| Stage | Scope | State |
|---|---|---|
| 1 | board description, components, build inputs | done |
| 2 | firmware (UniXNU UEFI app, DarwinBoot) | done |
| 3 | payload (kernel, ADT, trust cache) | done |
| 4 | boot chain hand-offs (loader -> UEFI -> DarwinBoot -> XNU) | done |
| 5 | kernel bring-up (platform expert, IOKit, prelinked kexts) | **done** |
| 6 | storage (UFS) | **conditions 1-4 done, 5 partial** (see below) |
| 7 | userland (root volume, `launchd`) | blocked by stage 6 |

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
| 6 persistence, 7 performance | not started | |

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

- [docs/TASKS.md](docs/TASKS.md) — consolidated task and status log
- [docs/STATUS-2026-11-11.md](docs/STATUS-2026-11-11.md) — stage-by-stage status
- [docs/BOOT-CHAIN.md](docs/BOOT-CHAIN.md) — boot chain against the official description
- [docs/UFS-QCOM-PHY-PORT.md](docs/UFS-QCOM-PHY-PORT.md) — restored PHY tables and call order
- [docs/HARDWARE-INVENTORY.md](docs/HARDWARE-INVENTORY.md) — hardware facts with sources

## Licence

Patches and board code follow the upstream UniXNU licensing (BSD-2-Clause-Patent where noted in the
individual files). Vendor-derived tables are data recovered from the device's own firmware.

## Current status — 2026-11-22

Stage 6 (UFS) conditions 1-4 are done and **both root windows now publish**: with the official
image written to the last 9 GiB of `userdata` (absolute LBA 246,662,139) the kernel prints

```
NATIVE_UFS: cold init 0
NATIVE_UFS: adopt bind 1
NATIVE_UFS root: fixed lba=eb3c3fb
NATIVE_UFS read: lba=eb3c3fb b0=53484e47      (SHNG manifest)
NativeUfs: published SystemOS
NativeUfs: SystemOS window started (noverify)
NativeUfs: published OS
NativeUfs: OS window started (noverify)
NATIVE_UFS: publication done=1 published=2
```

Open blocker: `IOBlockStorageDriver::instantiateMediaObject()` initialises every media with a
hard-coded empty content hint, so the in-kernel APFS container never matches our windows and no
root volume (hence no `root_match_volume_uuid`, no `launchd`) can appear. The planned fix is to
build `refer/IOStorageFamily` into `libIOStorageFamily.a`, take the hint from
`kIOMediaContentHintKey` on the provider, force-load it into the kernel and drop the kernelcache
copy from the prelink closure. The archive builds and carries a marker, but the kernel link still
skips it because `./unixnu kernel` reuses cached objects — see
[docs/HANDOFF-2026-11-22.md](docs/HANDOFF-2026-11-22.md) for the exact state, deviations and next
steps. Work continues in a different agent session from that handoff.
