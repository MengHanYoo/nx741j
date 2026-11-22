#!/bin/bash
# Apply build-friendly Linux VM settings inside this WSL distribution.
set -eu

target=/etc/sysctl.d/99-unixnu-build.conf
cat >"$target" <<'EOF'
# NX741J UniXNU build host: keep source/cache pages and avoid swap during
# parallel clang/LLVM work, while allowing bounded writeback bursts.
vm.swappiness = 10
vm.vfs_cache_pressure = 50
vm.dirty_background_bytes = 134217728
vm.dirty_bytes = 536870912
EOF
sysctl --system
