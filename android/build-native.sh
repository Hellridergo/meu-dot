#!/usr/bin/env bash
# Builds an arm64 Android 12+ library without any application credentials.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?Pass a native build directory}"
mkdir -p "$out"; out="$(cd "$out" && pwd)"
ndk="${ANDROID_NDK_HOME:-${ANDROID_HOME:?Define ANDROID_HOME}/ndk/28.2.13676358}"
test -f "$ndk/build/cmake/android.toolchain.cmake" || { echo "Set ANDROID_NDK_HOME to NDK r28c." >&2; exit 1; }
source_dir="${WHISPER_SOURCE_DIR:-$out/upstream}"
revision=d1be6fde11ac6e0407606b4e42fe72d34add8037
archive_sha=645445da884df6dbd52b30f82d7be7c01d1767c938714e477e786b88d529e9f4
if [[ ! -f "$source_dir/include/whisper.h" ]]; then
    mkdir -p "$source_dir"
    curl --fail --location --proto '=https' "https://codeload.github.com/ggml-org/whisper.cpp/tar.gz/$revision" -o "$out/source.tgz"
    python3 - "$out/source.tgz" "$archive_sha" <<'PY'
import hashlib,sys
with open(sys.argv[1],'rb') as f:
    if hashlib.sha256(f.read()).hexdigest()!=sys.argv[2]: raise SystemExit('whisper.cpp archive digest mismatch')
PY
    tar -xzf "$out/source.tgz" -C "$source_dir" --strip-components=1
fi
# ggml's asserts embed __FILE__. Map the build machine's absolute paths to
# neutral names so the APK carries no local user name or folder layout.
prefix_map="-ffile-prefix-map=$source_dir=whisper.cpp -ffile-prefix-map=$here=android"
cmake -S "$here/native" -B "$out/obj-arm64" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$prefix_map" -DCMAKE_CXX_FLAGS="$prefix_map" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-31 -DANDROID_STL=c++_static \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DWHISPER_SOURCE_DIR="$source_dir"
cmake --build "$out/obj-arm64" --target dot_voice --parallel 5
mkdir -p "$out/lib/arm64-v8a"
strip_tool=("$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-strip)
"${strip_tool[0]}" --strip-unneeded "$out/obj-arm64/libdot_voice.so" -o "$out/lib/arm64-v8a/libdot_voice.so"
