# WSL alignment — 2026-11-22

## Host profile

- CPU: AMD Ryzen 9 5950X, 16C/32T, 3.4 GHz base.
- Physical RAM: 31.91 GiB; currently about 16.88 GiB available during the check.
- System: ASUS AMI BIOS 4101, Windows 11 build family.
- E: 1.91 TiB total, about 513 GiB free; WSL VHDX is 143.2 GB virtual size.

## Storage layout

- Project inputs, build trees and logs are under `E:\default-workspace`.
- The WSL filesystem is stored in `E:\wsl\Ubuntu\ext4.vhdx`; this is the preferred
  Linux build filesystem because building on the `E:` 9p mount is much slower.
- The old C-drive `deepseek-harness\default-workspace` tree was copied to
  `E:\default-workspace\z80u-collect\archive\deepseek-harness-default-workspace-2026-11-22`,
  verified with per-file SHA-256, then removed.

## Runtime configuration

Current `.wslconfig`:

```ini
[wsl2]
processors=24
memory=20GB
swap=8GB
swapFile=E:/wsl/swap.vhdx

[experimental]
autoMemoryReclaim=gradual
sparseVhd=true
```

The repository now resolves the build tree and UEFI tree from explicit
arguments, `UNIXNU`/`UNIXNU_UEFI`, `/opt/work`, `/home/loongko`, or the E-drive
mirror instead of assuming one home directory.

## Build entry points

- `scripts/build-and-test.sh` uses the resolved UniXNU tree, UEFI tree and
  `unixnu.local.toml`; its smoke step uses `$UEFI/Mu_Basecore`.
- The kernel build uses 16 jobs, smoke uses 8, and the fixture QEMU gate uses
  8 GB RAM and 8 vCPUs. These values can be overridden with `JOBS`,
  `SMOKE_JOBS`, `QEMU_MEM`, and `QEMU_SMP`.
- `scripts/qemu-nx741j.sh` uses the same UEFI resolver and the tree passed by
  the gate. Fixture ADT generation remains the default; `QEMU_ADT=real` uses
  `scripts/mk-qemu-adt.py`, which now accepts `--tree` or `UNIXNU`.
- `scripts/build-fast.sh` and `scripts/build-and-qemu-fast.sh` no longer copy
  artifacts to a hard-coded Windows path.
- `E:\unixnu-build\UniXNU\unixnu.local.toml` no longer overrides the device
  command line with the old `-v serial=3`. The source of truth is
  `board.toml [payload].kernel_args`.
- `scripts/sync-framework-to-wsl.sh` first applies or verifies the complete
  pinned framework patch, then copies its changed runtime files into the active
  ext4 tree and backs up overwritten files under `out/sync-backup/`.
- `scripts/prepare-local-inputs.sh` regenerates the accepted flat trust-cache
  bundle before payload creation and verifies its board-pinned SHA-256.
- `scripts/run-local-gate.ps1` runs the sync, environment check and full
  `tree → kernel → payload → firmware → smoke → QEMU` gate from a normal
  PowerShell session.

## QEMU distinction

The full gate uses QEMU virt with the real payload and a generated fixture ADT.
`QEMU_ADT=real` is an explicit diagnostic mode that preserves the phone device
nodes and only relocates DRAM. It is not the normal boot gate.

## Current verification

The root WSL session is operational through
`C:\Program Files\WSL\wsl.exe -d Ubuntu -u root`. The environment check sees
24 CPUs, about 19.5 GiB RAM, the complete toolchain and
`qemu-system-aarch64`. Kernel, payload, firmware and the standalone QEMU
boot-chain gate have run from this session. The latest aggregate smoke result is
recorded separately because intermittent AAVMF/TCG boot stalls were explicitly
deferred by the user.
