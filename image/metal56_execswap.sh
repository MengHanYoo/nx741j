#!/bin/bash
# metal56: same kernel as METAL55, payload adds sweetexecswap=assetsd:/usr/libexec/assetsdwrap
set -euo pipefail
R=/mnt/e/default-workspace/z80u-collect/repos/nx741j
C=/mnt/e/default-workspace/z80u-collect
K=/opt/work/plk/k34-amfi.macho
ADTFLAGS="--root-extents $C/iosroot_extents.json --log-extents $C/ioslog_extents.json --fw-extents $C/iosfw_extents.json"
TRUSTCACHE="/opt/work/ipsw/Firmware/087-86698-044.dmg.trustcache+/opt/work/gpu/metal-out/plat2/plugin7.trst /opt/work/ipsw/Firmware/087-86593-047.dmg.trustcache+/opt/work/ipsw/Firmware/087-86715-047.dmg.trustcache"
EXTRA="ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 sweetfb_hz=120 sweetfb_fill=1 pandora_scanout=0 pandora_touch_gpio117=0 pandora_gpu=15 pandora_gpu_metal=1 sweetsample=lsd,SpringBoard sweetexecswap=assetsd:/usr/libexec/assetsdwrap"
cd /opt/work
D22AP_ADT=/opt/work/ipsw/d22ap.adt KERNEL="$K" TRUSTCACHE="$TRUSTCACHE" ADTFLAGS="$ADTFLAGS" CPUS=8 RD=ufsroot0 DEBUG=0xc NAME=metal56 \
  bash <(tr -d '\r' < "$R/wsl/make_phone_payload_nx741j.sh") /opt/work/run1 "$EXTRA" 2>&1 | tail -4
cp "$R/shim/loader/metal56/payload.bin" "$R/shim/loader/out/payload.bin"
P="$R/shim/loader/metal56/payload.bin"
ls -la "$P"
for s in assetsdwrap sweetexecswap "vm alias"; do
  printf "payload %-14s %s\n" "$s" "$(grep -c "$s" "$P" || true)"
done
echo DONE_PAYLOAD56