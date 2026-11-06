#!/usr/bin/env bash
# Android SDK, NDK r28c, CMake/Ninja and JDK 17 or newer.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?Pass an output directory outside the source tree}"
mkdir -p "$out"; out="$(cd "$out" && pwd)"
sdk="${ANDROID_HOME:?Define ANDROID_HOME}"
jdk="${JAVA_HOME:?Define JAVA_HOME}"
bt="$sdk/build-tools/${BUILD_TOOLS:-36.1.0}"
api="$sdk/platforms/android-35/android.jar"
export PATH="$jdk/bin:$PATH"
python3 "$here/cloud-dependencies.py" "$out/provider-jars"
jars=("$out/provider-jars/Java-WebSocket-1.6.0.jar" "$out/provider-jars/slf4j-api-2.0.16.jar" "$out/provider-jars/slf4j-nop-2.0.16.jar" "$out/provider-jars/msgpack-core-0.9.10.jar")
classpath="$api"
for entry in "${jars[@]}"; do classpath="$classpath:$entry"; done
if [[ -n "${DOT_NATIVE_PREBUILT:-}" ]]; then
    # For UI/Java-only work, reuse a previously verified unchanged JNI build.
    test -f "$DOT_NATIVE_PREBUILT/lib/arm64-v8a/libdot_voice.so"
    mkdir -p "$out/native/lib/arm64-v8a"
    cp "$DOT_NATIVE_PREBUILT/lib/arm64-v8a/libdot_voice.so" "$out/native/lib/arm64-v8a/"
else
    bash "$here/build-native.sh" "$out/native"
fi
stage="$(mktemp -d "$out/stage.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/classes" "$stage/dex" "$stage/java-res/META-INF/services"
"$bt/aapt2" compile --dir "$here/res" -o "$stage/res.zip"
"$bt/aapt2" link -A "$here/assets" -I "$api" --manifest "$here/AndroidManifest.xml" -o "$stage/unsigned.apk" "$stage/res.zip"
"$jdk/bin/javac" -source 8 -target 8 -encoding UTF-8 -classpath "$classpath" -d "$stage/classes" "$here"/src/br/bolso/dot/*.java
"$jdk/bin/jar" cf "$stage/classes.jar" -C "$stage/classes" .
"$bt/d8" --min-api 31 --lib "$api" --output "$stage/dex" "$stage/classes.jar" "${jars[@]}"
unzip -p "$out/provider-jars/slf4j-nop-2.0.16.jar" META-INF/services/org.slf4j.spi.SLF4JServiceProvider > "$stage/java-res/META-INF/services/org.slf4j.spi.SLF4JServiceProvider"
(cd "$stage/java-res" && zip -q "$stage/unsigned.apk" META-INF/services/org.slf4j.spi.SLF4JServiceProvider)
(cd "$stage/dex" && zip -q -j "$stage/unsigned.apk" classes*.dex)
(cd "$out/native" && zip -q -0 "$stage/unsigned.apk" lib/arm64-v8a/libdot_voice.so)
"$bt/zipalign" -f -P 16 4 "$stage/unsigned.apk" "$stage/aligned.apk"
# Signing key. Keep debug.p12 private and keep it: Android only installs an
# update signed with the same key. Set MEU_DOT_KEYSTORE_PASS to protect it with
# your own password (the default suits a throwaway local key only).
ks_pass="${MEU_DOT_KEYSTORE_PASS:-android}"
if [[ ! -f "$out/debug.p12" ]]; then
    (umask 077; "$jdk/bin/keytool" -genkeypair -keystore "$out/debug.p12" -storepass "$ks_pass" -keypass "$ks_pass" -alias dot-test -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=Dot de bolso local test")
fi
MEU_DOT_KEYSTORE_PASS="$ks_pass" "$bt/apksigner" sign --ks "$out/debug.p12" --ks-key-alias dot-test --ks-pass env:MEU_DOT_KEYSTORE_PASS --v4-signing-enabled false --out "$stage/Meu-Dot.apk" "$stage/aligned.apk"
"$bt/apksigner" verify --verbose "$stage/Meu-Dot.apk"
mv "$stage/Meu-Dot.apk" "$out/Meu-Dot.apk"
