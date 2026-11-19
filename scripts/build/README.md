# Build entry points

Scripts used to produce the artefacts in `artifacts/2026-11-19/`. They assume the UniXNU
checkout at `/opt/work/nx741j-build/UniXNU`, the device repository at `$R`, and the Linux
toolchain installed by `scripts/linux/setup-toolchain.py`.

| Script | Step |
|---|---|
| `noaes2_2017.sh` | kernel build with the no-native-AES configuration |
| `pay_step2b_2017.sh` | payload build; clears the payload cache first, then runs the QEMU gate |
| `qemu_step2_2017.sh` | QEMU gate: asserts `UNIXNU: BootEntry`, `MARK #29` and `EBS ok` |
| `fw_2017.sh` | firmware build (Mu volume plus the device drivers) |
| `patchbuild.sh` | copies the board hooks into the tree, applies them, and builds the kernel |

The paths inside the scripts are absolute (`/opt/work`, `/mnt/e`); adjust them when the
checkout moves.