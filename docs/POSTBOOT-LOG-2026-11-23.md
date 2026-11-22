# NX741J first postboot HFSX log — 2026-11-23

This is the read-only evidence extracted from the live pinned root after the
first consolidated write. The device was in 9008; no device write was
performed while collecting these records.

## HFSX volume

Before boot:

```text
Mount Count: 3
Number of files: 184164
Number of folders: 137206
Number of Free Blocks: 1136173
```

After boot:

```text
Last Mounted By: Mac OS X, Journaled
Volume Unmounted Improperly
Mount Count: 187887
Number of files: 185376
Number of folders: 139113
Number of Free Blocks: 1084366
```

The allocation and mount counters prove that XNU mounted and wrote the HFSX
root.

## launchd

```text
Successfully spawned SpringBoard[26] because system support
Successfully spawned backboardd[56] because speculative
Successfully spawned lsd[100] because ipc (mach)
Successfully spawned datamigrator[109] because ipc (mach)
com.apple.datamigrator exited due to exit(0)
```

The DataMigration and normal user daemons were running.

## Other live daemon records

```text
Mobile Activation Startup: product_type iPhone10,3
Mobile Activation Startup: hardware_model D22AP
dealwith_activation: Hactivation is enabled, short circuiting activation state to Activated

mobileactivationd: Activation State: Activated
keybagd: HealthPlistCopyOrCreateNew, no fatal error
usermanagerd: Loaded 1 user sessions
usermanagerd: BootedUser is:501
```

`containermanagerd` repeatedly logs:

```text
Data protection not supported for containers of class [2]
Data protection not supported for containers of class [4]
```

The same retry pattern exists in the earlier successful lockscreen root, so it
is not the distinguishing blocker.

## Missing display component

The first root had no:

```text
/System/Library/Extensions/NativeAdrenoMetal.bundle
/private/var/fuxi/iosfw.img
```

The first boot payload also omitted the successful arguments:

```text
pandora_gpu=14 pandora_gpu_metal=1
```

Therefore the Apple logo was the last boot-owned frame while launchd and
SpringBoard were already alive.

## GPU repair

The repair used:

```text
NativeAdrenoMetal  22,438,240 B
SHA-256 04ddea9f0415db9a2fae067521482aa14a938931c1a576cfcb797f7e48a56edf
```

This is the exact plugin binary from the earlier successful lockscreen root
under the `metal-out/plat` build, together with `plat/plugin.trst`.

The 2 MiB `PANDFW01` firmware file was allocated at HFS block
`3063695`, length `512` blocks. Through the pinned root extent table its
physical userdata range is:

```text
file offset 0
userdata block 5,738,895
length 512 blocks
```

The resulting root delta contains 5,594 changed blocks in 41 ranges. Every
range matched the live device on preflight and readback.
