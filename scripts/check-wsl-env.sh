#!/bin/bash
# Read-only alignment check for the E-drive workspace and its WSL build tree.
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=wsl-env.sh
source "$SCRIPT_DIR/wsl-env.sh"

TREE=$(resolve_unixnu_tree "${UNIXNU:-}") || {
  echo 'FAIL  UniXNU tree not found (set UNIXNU or pass a path to the build scripts)' >&2
  exit 1
}
UEFI=$(resolve_uefi_tree "${UNIXNU_UEFI:-}") || {
  echo 'FAIL  nx741j UEFI tree not found (set UNIXNU_UEFI)' >&2
  exit 1
}
ARTIFACTS=$(resolve_artifact_dir "${UNIXNU_ARTIFACTS:-}")

echo "kernel_tree=$TREE"
echo "uefi_tree=$UEFI"
echo "artifact_dir=$ARTIFACTS"
echo "cpu=$(nproc)"
echo "memory_kib=$(awk '/MemTotal/ {print $2}' /proc/meminfo)"
echo "wsl_vhdx=$(test -f /mnt/e/wsl/Ubuntu/ext4.vhdx && echo present || echo missing)"
TOOLCHAIN=${UNIXNU_TOOLCHAIN:-/opt/unixnu-linux}
echo "toolchain_root=$TOOLCHAIN"
echo "toolchain_clang=$(test -x "$TOOLCHAIN/llvm/bin/clang" && echo present || echo missing)"
for tool in python3 dtc qemu-system-aarch64 clang llvm-ar; do
  if command -v "$tool" >/dev/null 2>&1; then
    printf '%s=%s\n' "$tool" "$(command -v "$tool")"
  else
    printf '%s=missing\n' "$tool"
  fi
done

test -f "$TREE/unixnu" || { echo 'FAIL  unixnu entry point missing' >&2; exit 1; }
test -f "$TREE/unixnu.local.toml" || { echo 'FAIL  unixnu.local.toml missing' >&2; exit 1; }
test -f "$TREE/../UniXNU-nx741j/board.toml" || echo 'NOTE  device copy is not next to the build tree; pass -D explicitly'
echo 'PASS  paths resolve; build is ready for the QEMU gate'
