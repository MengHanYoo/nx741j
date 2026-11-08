#!/usr/bin/env bash
# Local build pipeline for NX741J (replaces GitHub Actions).
#
# Usage: scripts/ci-local.sh [--check] [--skip-firmware] [--skip-package]
#   --check          resolve + assemble the tree + porting-record check only
#   --skip-firmware  skip the UEFI firmware build
#   --skip-package   skip the package step
#
# Everything runs in the local WSL UniXNU checkout (default ../UniXNU, override with
# UNIXNU=DIR). No GitHub, no network beyond the already-fetched sources.
set -euo pipefail
DEV=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
UNI=${UNIXNU:-$(cd "$DEV/../UniXNU" 2>/dev/null && pwd || true)}
[ -n "$UNI" ] && [ -x "$UNI/unixnu" ] || { echo "ci-local: set UNIXNU to the UniXNU checkout" >&2; exit 1; }
CHECK=0; SKIP_FW=0; SKIP_PKG=0
for a in "$@"; do
  case "$a" in
    --check) CHECK=1 ;;
    --skip-firmware) SKIP_FW=1 ;;
    --skip-package) SKIP_PKG=1 ;;
    *) echo "ci-local: unknown option $a" >&2; exit 2 ;;
  esac
done

cd "$UNI"
step() { printf '\n== %s\n' "$1"; }

step 'defconfig'
./unixnu defconfig nx741j_defconfig -D "$DEV"
step 'fetch'
./unixnu fetch -D "$DEV"
step 'status'
./unixnu status --json -D "$DEV" | python3 -c 'import json,sys; d=json.load(sys.stdin); print("configured", d.get("configured"), "patches", len(d.get("patch_order") or []))'
step 'tree'
./unixnu tree -D "$DEV"
step 'porting record'
python3 scripts/porting-record.py check --device "$DEV"
if [ "$CHECK" = 1 ]; then echo 'ci-local: check-only run complete'; exit 0; fi

step 'kernel'
./unixnu kernel -D "$DEV"
step 'payload'
./unixnu payload -D "$DEV"
step 'boot image'
./unixnu bootimg -D "$DEV"
if [ "$SKIP_PKG" = 0 ]; then step 'package'; ./unixnu build package -D "$DEV"; fi
if [ "$SKIP_FW" = 0 ]; then step 'firmware'; ./unixnu build firmware -D "$DEV"; fi

OUT="$UNI/out/nx741j/ios-16.7.16-20H392/nx741j"
step 'artifacts'
sha256sum "$OUT/kernel/kernel.macho" "$OUT/payload/payload.img" "$OUT/boot/boot.img"
echo 'ci-local: done'
