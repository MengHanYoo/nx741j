#!/bin/bash
# Sync only the framework files changed by this port from the E-drive mirror
# into the active ext4 build tree. Device scripts and board files stay in the
# device repository and are overlaid by `unixnu tree`.
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=wsl-env.sh
source "$SCRIPT_DIR/wsl-env.sh"

SOURCE=${1:-${UNIXNU_SOURCE:-/mnt/e/unixnu-build/UniXNU}}
TARGET=$(resolve_unixnu_tree "${2:-${UNIXNU:-}}") || {
  echo "cannot resolve target UniXNU tree" >&2
  exit 1
}

[ -x "$SOURCE/unixnu" ] || { echo "source tree has no unixnu: $SOURCE" >&2; exit 1; }
"$SCRIPT_DIR/apply-framework-patch.sh" "$SOURCE"
SOURCE=$(cd "$SOURCE" && pwd -P)
TARGET=$(cd "$TARGET" && pwd -P)
if [ "$SOURCE" = "$TARGET" ]; then
  echo "source and target are the same tree; nothing to sync"
  exit 0
fi

backup="$TARGET/out/sync-backup/$(date +%Y%m%d-%H%M%S)"
files=(
  unixnu.local.toml
  scripts/build-xnu-4k-baseline.sh
  scripts/unixnu/kernel.py
  kits/XnuPrelink/native_prelink.py
  kits/XnuPrelink/emit_prelink.py
  kits/XnuPrelink/test_native_prelink.py
  kits/XnuPrelink/README.md
  docs/concepts/prelink.md
  docs/zh/concepts/prelink.md
)

for relative in "${files[@]}"; do
  src="$SOURCE/$relative"
  dst="$TARGET/$relative"
  [ -f "$src" ] || { echo "missing source file: $src" >&2; exit 1; }
  if [ -f "$dst" ] && cmp -s "$src" "$dst"; then
    continue
  fi
  mkdir -p "$backup/$(dirname "$relative")" "$(dirname "$dst")"
  if [ -f "$dst" ]; then
    cp -a "$dst" "$backup/$relative"
  fi
  cp -a "$src" "$dst"
  echo "synced $relative"
done

echo "source=$SOURCE"
echo "target=$TARGET"
echo "backup=$backup"
