#!/usr/bin/env bash
#
# scripts/package.sh - one command from a clean checkout to the boot image and payload.
#
# Interface, per https://unixnu.com/zh/porting/packaging:
#   scripts/package.sh [--os 17|27|PROFILE] [--no-deps] [--check] [unixnu options...]
#
# Steps (a failure names the step and stops):
#   deps       fetch git sources and write unixnu.lock
#   preflight  select the defconfig for this OS profile and assemble the build tree
#   patches    print the resolved XNU patch order
#   build      kernel, prelink (when configured), package, payload, firmware
#   pack       boot image from the firmware volume, device tree and payload
#   verify     write package.manifest.json and check every artifact against it
#
# Every board value is read from board.toml; this script holds no device name, path, size or version.
# Local inputs stay in the Git-ignored unixnu.local.toml / local.toml of the UniXNU checkout.
set -euo pipefail

DEV=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

step() { printf 'step %s\n' "$1"; }
die() { printf 'scripts/package.sh: %s\n' "$*" >&2; exit 1; }

OS_ARG=""
SKIP_DEPS=0
CHECK_ONLY=0
PASSTHROUGH=()
while [ $# -gt 0 ]; do
    case "$1" in
        --os)       [ $# -ge 2 ] || die "--os needs an OS profile"; OS_ARG=$2; shift 2 ;;
        --os=*)     OS_ARG=${1#--os=}; shift ;;
        --no-deps)  SKIP_DEPS=1; shift ;;
        --check)    CHECK_ONLY=1; shift ;;
        -h|--help)  awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "${BASH_SOURCE[0]}"; exit 0 ;;
        *)          PASSTHROUGH+=("$1"); shift ;;
    esac
done

# --- board inputs -----------------------------------------------------------
# Emitted as shell assignments; values are validated below before use.
eval "$(python3 - "$DEV/board.toml" "$OS_ARG" <<'PY'
import shlex, sys, tomllib
path, arg = sys.argv[1], sys.argv[2]
try:
    board = tomllib.load(open(path, 'rb'))
except FileNotFoundError:
    sys.exit(f'{path}: missing')
b = board.get('board', {})
name, os_profile = b.get('name', ''), b.get('os_profile', '')
profiles = board.get('package', {}).get('defconfig', {})
if arg:
    if arg in profiles:
        profile = arg
    else:
        matches = [p for p in profiles if p.split('-')[0] == arg or p.startswith(arg + '.')]
        if len(matches) != 1:
            sys.exit(f'--os {arg}: no [package.defconfig] entry (have: {", ".join(profiles) or "none"})')
        profile = matches[0]
else:
    profile = os_profile
defconfig = profiles.get(profile, '')
if not defconfig:
    king = board.get('kernel', {}).get('defconfig', '')
    if profile == os_profile and king:
        defconfig = king
    else:
        sys.exit(f'{profile}: add [package.defconfig] "{profile}" to board.toml')
unixnu = board.get('package', {}).get('unixnu', '../UniXNU')
config = defconfig[:-len('_defconfig')] if defconfig.endswith('_defconfig') else defconfig
print('DEVICE_NAME=' + shlex.quote(name))
print('OS_PROFILE=' + shlex.quote(profile))
print('DEFCONFIG=' + shlex.quote(defconfig))
print('CONFIG_NAME=' + shlex.quote(config))
print('UNIXNU_REL=' + shlex.quote(unixnu))
print('HAS_PRELINK=%d' % (1 if board.get('kernel', {}).get('prelink') else 0))
print('BOOT_MAX=%s' % shlex.quote(str(board.get('package', {}).get('boot_partition_bytes') or 0)))
PY
)"

[ -n "${DEVICE_NAME:-}" ] || die "board.toml: [board] name is required"
[ -n "${OS_PROFILE:-}" ] || die "board.toml: [board] os_profile is required"
UNIXNU=$(cd "$DEV/$UNIXNU_REL" 2>/dev/null && pwd) || die "[package] unixnu = $UNIXNU_REL: not a directory"
[ -x "$UNIXNU/unixnu" ] || die "$UNIXNU/unixnu: missing; check [package] unixnu in board.toml"
OUT="$UNIXNU/out/$DEVICE_NAME/$OS_PROFILE/$CONFIG_NAME"
UNIXNU_ARGS=(-D "$DEV" "${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}")
run_unixnu() { ( cd "$UNIXNU" && ./unixnu "$@" "${UNIXNU_ARGS[@]}" ); }

# --- steps ------------------------------------------------------------------
if [ "$SKIP_DEPS" = 0 ]; then
    step deps
    run_unixnu fetch || die "step deps failed"
fi

step preflight
run_unixnu defconfig "$DEFCONFIG" || die "step preflight failed (defconfig)"
run_unixnu tree                     || die "step preflight failed (tree)"

step patches
[ -f "$OUT/xnu-patches.list" ] || die "step patches failed: $OUT/xnu-patches.list missing"
printf '  %s patches, in application order:\n' "$(wc -l < "$OUT/xnu-patches.list")"
sed 's/^/  /' "$OUT/xnu-patches.list"

if [ "$CHECK_ONLY" = 1 ]; then
    printf 'check ok: deps, preflight and patch order resolve for %s\n' "$OS_PROFILE"
    exit 0
fi

step build
run_unixnu build kernel  || die "step build failed (kernel)"
if [ "$HAS_PRELINK" = 1 ]; then
    run_unixnu prelink   || die "step build failed (prelink)"
else
    printf '  [kernel.prelink] is not set in board.toml; skipping prelink\n'
fi
run_unixnu build package || die "step build failed (package)"
run_unixnu build payload || die "step build failed (payload)"
if printf '%s\n' "${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}" | grep -qx -- '--uefi' \
   || printf '%s\n' "${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}" | grep -q -- '^--uefi=' \
   || [ -n "${UNIXNU_UEFI:-}" ]; then
    run_unixnu build firmware || die "step build failed (firmware)"
else
    printf '  no --uefi/UNIXNU_UEFI: skipping the firmware step\n'
fi

step pack
run_unixnu bootimg || die "step pack failed"

step verify
python3 - "$OUT" "$DEVICE_NAME" "$OS_PROFILE" "$CONFIG_NAME" "$BOOT_MAX" <<'PY' || die "step verify failed"
import hashlib, json, os, sys
from pathlib import Path
out = Path(sys.argv[1])
device, profile, config, boot_max = sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5] or 0)
wanted = ['kernel/kernel.macho', 'payload/payload.img', 'boot/boot.img']
entries, missing = [], []
for rel in wanted + ['.config', 'xnu-patches.list']:
    path = out / rel
    if not path.is_file():
        if rel in wanted:
            missing.append(rel)
        continue
    data = path.read_bytes()
    entries.append({'path': rel, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
if missing:
    sys.exit('missing artifacts: ' + ', '.join(missing))
boot = out / 'boot/boot.img'
if boot_max and boot.stat().st_size > boot_max:
    sys.exit(f'boot.img {boot.stat().st_size} B exceeds [package] boot_partition_bytes {boot_max}')
kernel_args = None
pay = out / 'payload/payload.json'
if pay.is_file():
    kernel_args = json.loads(pay.read_text()).get('kernel_args')
manifest = {'device': device, 'os_profile': profile, 'configuration': config,
            'kernel_args': kernel_args, 'artifacts': entries}
target = out / 'package.manifest.json'
target.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
for item in entries:
    print(f'  {item["path"]:<24} {item["bytes"]:>10} B  {item["sha256"][:16]}')
print(f'  wrote {target}')
PY

printf 'package.sh: %s/%s/%s done\n' "$DEVICE_NAME" "$OS_PROFILE" "$CONFIG_NAME"
