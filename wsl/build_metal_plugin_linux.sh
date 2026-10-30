#!/bin/bash
# Linux 本地构建 NativeAdrenoMetal.bundle（对应 macOS 的 tools/metal/build_plugin.sh，
# 用 clang-ios + ld64.lld + ldid 取代 xcrun/codesign；跳过需要 Apple metal 编译器的 mtltest）
set -euo pipefail
G=/opt/work/gpu
U=$G/UniXNU
NM=$G/NativeMetal
NA=$G/NativeAdreno
M2V=$G/m2v-src
MESA=$G/mesa-src
ROOT=/mnt/e/default-workspace/z80u-collect/repos/nx741j
OUT=$G/metal-out
B=$OUT/build
CC=/usr/local/bin/clang-ios
export PATH=/root/.cargo/bin:$PATH

mkdir -p "$B/gen" "$OUT"

echo '== 0. NativeMetal 按 iOS 16 适配'
python3 "$ROOT/tools/metal/adapt_ios16.py" "$NM" > /tmp/adapt16.log 2>&1 ||
  echo "   (source already adapted, skipping)"
tail -3 /tmp/adapt16.log 2>/dev/null || true

echo '== 1. smoke shaders'
{
  echo '// Generated for the local Linux build; do not edit.'
  echo '#pragma once'
  echo '#include <stdint.h>'
  for s in "$NM"/shaders/smoke_*; do
    n=$(basename "$s")
    glslc --target-env=vulkan1.1 -O "$s" -o "$B/$n.spv"
    spirv-val --target-env vulkan1.1 "$B/$n.spv"
    python3 - "$B/$n.spv" "$n" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
w = struct.unpack('<%dI' % (len(d) // 4), d)
print('static const uint32_t kSpv_%s[] = {' % sys.argv[2].replace('.', '_'))
for i in range(0, len(w), 8):
    print('    ' + ', '.join('0x%08x' % x for x in w[i:i+8]) + ',')
print('};')
PY
  done
} > "$B/gen/NativeTurnipShaders.h"
ls -la "$B/gen/NativeTurnipShaders.h"

echo '== 2. metal2vulkan 补丁'
for p in "$NM"/bridge/patches/000*.patch; do
  echo "   $(basename "$p")"
  git -C "$M2V" apply "$p" 2>/dev/null || echo "     (已应用或跳过)"
done

echo '== 3. AIR fixtures'
llvm-as -o "$B/runtime_examples.air" "$M2V/crates/metal2vulkan-validation/fixtures/public/examples/runtime_examples.ll"
llvm-as -o "$B/function_constant.air" "$M2V/crates/metal2vulkan-validation/fixtures/public/kernel_function_constant_gate_fold.ll"
ls -la "$B"/*.air

echo '== 4. Rust 桥（aarch64-apple-ios）'
cat > "$B/Cargo.toml" <<EOF
[package]
name = "native-metal-bridge"
version = "0.1.0"
edition = "2021"
[workspace]
[lib]
crate-type = ["staticlib"]
path = "$NM/bridge/NativeMetalBridge.rs"
[dependencies]
metal2vulkan = { path = "$M2V/crates/metal2vulkan", features = ["ash"] }
metal2vulkan-runtime = { path = "$M2V/crates/metal2vulkan-runtime" }
libc = "0.2"
sha2 = { version = "0.10" }
[profile.release]
panic = "unwind"
debug = 1
EOF
RID=$( (git -C "$M2V" rev-parse HEAD; git -C "$M2V" diff HEAD) | sha256sum | cut -c1-32)
echo "runtime id: $RID"
NMB_RUNTIME_ID=$RID NMB_FIXTURE_AIR="$B/runtime_examples.air" NMB_FC_AIR="$B/function_constant.air" \
  cargo build --release --manifest-path "$B/Cargo.toml" --target aarch64-apple-ios --target-dir "$B/target" 2>&1 | tail -12
BRIDGE=$B/target/aarch64-apple-ios/release/libnative_metal_bridge.a
ls -l "$BRIDGE"

echo '== 5. 插件 bundle'
BUNDLE=$OUT/NativeAdrenoMetal.bundle
rm -rf "$BUNDLE"; mkdir -p "$BUNDLE"
$CC -std=c11 -O2 -DNDEBUG -c "$U/kits/AdrenoPM4/AdrenoPM4.c" -o "$B/AdrenoPM4.o"
$CC -std=c++17 -O2 -DNDEBUG -fno-exceptions -fobjc-arc -Wno-deprecated-declarations -bundle \
  -I "$NA" -I "$U/kits/AdrenoPM4" -I "$MESA/include" -I "$B/gen" \
  -x objective-c++ "$NM/NativeAdrenoMetalPlugin.mm" "$NM/NativeBridgeMetalFrontend.mm" -x none \
  "$B/AdrenoPM4.o" "$BRIDGE" -Wl,-dead_strip -lc++ -lc++abi -lobjc -framework Foundation -framework Metal -framework IOSurface \
  -o "$BUNDLE/NativeAdrenoMetal"
/usr/bin/llvm-strip -x "$BUNDLE/NativeAdrenoMetal" || true
ls -la "$BUNDLE"

echo '== 6. Info.plist'
python3 - "$BUNDLE" <<'PY'
import plistlib, sys, os
b = sys.argv[1]
d = {
 'CFBundleIdentifier': 'org.blu-sharky.NativeAdrenoMetal',
 'CFBundleExecutable': 'NativeAdrenoMetal',
 'CFBundlePackageType': 'BNDL',
 'CFBundleVersion': '1',
 'CFBundleShortVersionString': '1',
 'NSPrincipalClass': 'NativeAdrenoMetalDevice',
 'MinimumOSVersion': '16.0',
}
with open(os.path.join(b, 'Info.plist'), 'wb') as f:
    plistlib.dump(d, f, fmt=plistlib.FMT_XML)
print('Info.plist written')
PY

echo '== 7. 签名（ldid 代替 codesign）'
ldid -S"$BUNDLE/NativeAdrenoMetal" 2>&1 | head -3 || ldid -S "$BUNDLE/NativeAdrenoMetal"
file "$BUNDLE/NativeAdrenoMetal"
sha256sum "$BUNDLE/NativeAdrenoMetal" | tee "$OUT/SHA256SUMS"
echo METAL_LOCAL_DONE
