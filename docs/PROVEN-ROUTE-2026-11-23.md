# NX741J proven iOS route — 2026-11-23

> This document supersedes the Mu/UEFI diagnostic route as the device bring-up
> path. The source tree keeps the UEFI work for later cleanup, but the image
> installed on the phone uses the locally proven shim-loader route.

## Two independent entry points

The device has two separate mechanisms that can transfer control from the
bootloader. They must not be mixed in one diagnosis.

### A. ABL -> boot_a (current proven iOS route)

```text
XBL/ABL
  -> boot_a: Android v4 header + NX741J loader Image
  -> embedded SWEETXNU payload
  -> XNU 8796.141.3
  -> HFSX ufsroot0
  -> launchd
```

This is the route that previously reached the iOS lockscreen, userspace,
display, touch and Metal work on this unit. The current `boot_a` contains the
loader and its SWEETXNU payload directly. The stock efisp is restored when
removing a custom P7 image, but efisp is not the source of the `boot_a` payload.

For development runs, data-a-live uses the RAM-only form of this entry:

```text
fastboot boot loader-boot.img
```

No `flash`, `flashall`, `erase`, or partition write is part of this operation.
Power cycling restores the installed Android boot path.

### B. efisp -> rawdump -> Mu UEFI (diagnostic route)

```text
efisp P7
  -> rawdump payload + SILICIUM_UEFI.fd
  -> Mu BDS
  -> UniXNUDxe -> UniXNU.efi -> DarwinBoot -> XNU
```

This route stopped at Mu handle enumeration (`SmemStubDxe: no handles to
connect`, `msbootpolicy.efi`, `Image not found`). It is retained for
diagnostics and is not the final device iOS route.

The most recent device write selected route A: it wrote the integrated
`boot_a`, restored the stock efisp to remove the custom P7 entry, and did not
write the Mu payload/FD or depend on Mu BDS.

## Inputs

| Input | Result |
|---|---|
| Base root | original iPhone X 20H392 IPSW build, SHA-256 `7fd949bb026ef84d22bf316f28c41234358e41c65e4f393e1a3d1ffe1731b6ea` |
| First patched root | `iosroot-NX741J-20H392-proven-20261122.img`, SHA-256 `dd7dee484eee2faf03484f5c9726682ed562fe7163dd3a362d167454df280ac4` |
| GPU delta target snapshot | SHA-256 `dc0a1d9fb4c7eefe1c8a36d7e1a2684a86d7d8f402340a08e98f2a88436e51b1` |
| Pinned extent table | 39 extents, 4,194,304 blocks, DMA below 36 bits |
| Live GPT | LUN0 `userdata` first `4,875,048`, last `248,838,138`, 4 KiB LBAs |
| Proven kernel | `/opt/work/plk/k39-amfi.macho`, SHA-256 `f28d3db2316269a9c6e4d2fa75e00ed598bf0aa419e97a87141e7e11407c86ba` |
| Payload | SHA-256 `e701e8a04b2db0786813f5b5daf734b0790dede279e932f5c76baee30542629e` |
| ADT | SHA-256 `84265ac82d7eb56b68c77915f1ccfa619cc454ccad9cfc950e7f4a0d3d3eb477` |
| Loader Image | SHA-256 `fa149280590bfefc0e0764bc66f62f2c878a2ad34adc20fb35fe186bb148a17f` |
| `boot_a` image | SHA-256 `1417864756da79f212856ef20debe855ced8cca6beec431d243d343f10624398` |
| Metal plugin | SHA-256 `04ddea9f0415db9a2fae067521482aa14a938931c1a576cfcb797f7e48a56edf` |
| Stock efisp | SHA-256 `4df183594a63af28c681e48b1d9fdb0305ee055ea8fde92118a1e0af7a5f6f17` |

The patched root was derived from the original IPSW root; `iosroot-GOLDEN.img`
was not used as a build input.

## Root fixes

The plain data-a-live-style root reached launchd but lacked NX741J-specific
container state. `scripts/apply-nx741j-root-bringup.py` adds:

- `systemgroup.com.apple.mobilegestaltcache` with the required container
  metadata and an empty version-1 `com.apple.MobileGestalt.plist`;
- the 12-entry launchd disabled table for baseband, Bluetooth, FairPlay,
  DriverKit, BootPD, DHCPv6, relevance and Siri test jobs;
- SpringBoard `SBDontDimOrLockOnAC`, `SBDisableIdleSleep`,
  `SBDisableAutoDim` and `BKNoWatchdogs`.

The complete patched image differs from the verified base image in only
13 blocks. They map to nine physically contiguous ranges in the pinned file:

| Item | Value |
|---|---|
| changed bytes | 53,248 |
| changed 4 KiB blocks | 13 |
| physical write ranges | 9 |
| userdata sectors | 5,743,400 through 9,516,625 |

`scripts/diff-pinned-root.py` maps logical blocks through the 39-extent table;
`scripts/extract-root-delta.py` emits and hashes the exact write files.

## Boot payload

The proven source path is:

```text
k39-amfi.macho
  + mkadt_nx741j.py --root-extents <current 39-extent JSON>
  + two original trust-cache modules
  -> mkpayload.py
  -> SWEETXNU v1, 16 segments
  -> WSL POSIX loader build
  -> Android boot header v4, kernel-only, no ramdisk
```

The loader source is mirrored in the same GitHub repository on the
[`proven-loader`](https://github.com/MengHanYoo/nx741j/tree/proven-loader)
branch. `scripts/build-proven-route.sh` expects a local clone at
`E:\default-workspace\z80u-collect\repos\nx741j` and accepts `LEGACY=<path>`
to move it.

The ADT deliberately does not contain stale `pandora-log-extents` or
`pandora-fw-extents`. Those old mappings belonged to the previous root and
could have written through reused userdata blocks.

Boot arguments are the proven first-lockscreen set:

```text
debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress
rd=ufsroot0 ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1
wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1
sweetfb_hz=30 sweetfb_fill=1 pandora_scanout=0
pandora_touch_gpio117=0 pandora_gpu=14 pandora_gpu_metal=1
sweetsample=MobileGestaltHel,containermanage,cfprefsd,lsd,SpringBoard
```

## Validation

Static gate:

```sh
scripts/check-proven-route.py \
  --boot <loader-boot.img> \
  --root <proven-root.img> \
  --extents <pinfile-extents.json> \
  --adt-tools <nx741j/tools>
```

It verifies the Android v4 header, raw arm64 Image, zero ramdisk,
`SWEETXNU`, all 16 segments, ADT root mapping, absence of stale physical
mappings, the two trust caches, boot arguments and the 16 GiB root hash.

QEMU runs the same loader and XNU image without the Mu/DarwinBoot smoke path.
The kernel reaches interrupt-controller setup and stops at:

```text
SWEET GIC: virtual timer INTID 27
panic: find_gicr_pe_base: cannot find GICR base for core 0 @pe_fiq.c:89
```

That is expected for QEMU `virt`: the ADT correctly contains the phone's
`GICR@0x17080000`, which QEMU does not model. The result proves loader,
payload, ADT, trust cache and XNU entry are intact; it does not model NX741J
UFS, GIC, display, touch or PMGR.

## Real-device write

`scripts/host/write-proven-route-9008.ps1` performed one Firehose session:

1. read the live 4K GPT and require the pinned `userdata` bounds;
2. read all nine changed root ranges and require their original hashes;
3. write the 13 changed root blocks;
4. restore stock efisp;
5. write `boot_a`;
6. read every region back and compare;
7. read the GPT again and require it unchanged;
8. reset.

The write report is:

```text
out/proven-route/device-write-20261123-000200/write-report.json
```

All nine root ranges, stock efisp and `boot_a` matched on readback. The live
GPT remained `4,875,048..248,838,138`.

### First postboot log analysis

The first reset produced a real HFSX mount, launchd and userspace. Evidence
read back from the live pinned root:

| Item | Evidence |
|---|---|
| HFSX mounted read-write | `Last Mounted By: Mac OS X, Journaled`; write count advanced |
| Metadata allocated | files `184,164 -> 185,376`; folders `137,206 -> 139,113` |
| launchd | `launchd.log`, `launchd_cache_loader.log`, `logd.0.log` created |
| SpringBoard | `Successfully spawned SpringBoard[26] because system support` |
| backboardd | `Successfully spawned backboardd[56] because speculative` |
| lsd | `Successfully spawned lsd[...] because ipc (mach)` |
| DataMigration | `com.apple.datamigrator` spawned and exited `exit(0)` |

This disproves an early kernel, root-device or launchd failure. The last
boot-owned frame remained the Apple logo because the root built from the plain
IPSW lacked the proven `NativeAdrenoMetal.bundle`, had no GPU firmware file and
the payload omitted the successful GPU boot arguments.

### GPU-proven repair

The repair uses the exact plugin binary from the prior successful lockscreen
(`metal-out/plat`, SHA-256 `04ddea9f...`) and its matching
`plat/plugin.trst`. A 2 MiB `PANDFW01` file is written as
`/private/var/fuxi/iosfw.img`; its HFS allocation is mapped through
`/chosen/pandora-fw-extents`, so no Android file or unallocated F2FS block is
used.

The second write contained 5,594 changed blocks in 41 ranges (22,913,024 B).
Host preflight read all 41 ranges from the live device and matched every base
hash before writing. Every root range, stock efisp and the new `boot_a`
matched on readback; GPT remained unchanged.

After the first reset, Windows produced no `9008`, `QUSB_BULK`, Android or ADB
device for at least 45 seconds. After the GPU repair reset there was still no
`9008`, `QUSB_BULK`, Android or ADB device after 105 seconds. This removes the previous persistent
`QUSB_BULK_CID:046A_SN:098F0FC3` failure signature and is consistent with the
known iOS state where USB remains unavailable because step 7 is frozen.
Direct launchd/SpringBoard evidence is now captured from the live HFSX log;
the remaining observability item is a visible lock-screen capture after the
GPU repair, because step-7 USB remains frozen.

## Progress

| Official stage | NX741J state |
|---|---|
| 1 inputs and build tree | complete |
| 2 shared support and OS ABI | complete |
| 3 device repository and firmware data | complete |
| 4 build pipeline | complete |
| 5 hardware description / ADT | complete |
| 6 drivers | storage, display, touch, HID and CPU paths are in the proven kernel; persistent Data, sustained performance and final thermal policy remain |
| 7 USB/Wi-Fi/audio/baseband/camera/back display | frozen by owner |
| 8 records, startup and recovery | install/QEMU/readback and launchd/SpringBoard records complete; visible lock-screen capture remains an observability item |

Current overall engineering coverage is approximately **92%** for booting the
existing iOS 16.7.16 root. The step-7 subsystem work is intentionally excluded
from that number. The former 80% figure described the unfinished
Mu/NativeUfs/APFS route and no longer applies.
