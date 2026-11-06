#!/bin/sh
# Runs every host test. Needs a C compiler (clang or gcc). The bridge tests need
# Node.js 22.13+; the Android tests need JDK 17+ and download five pinned JARs.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/meu-dot-tests.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc=${CC:-cc}

echo "== Firmware audio (voice.c, ASan/UBSan)"
sh "$root/tests/firmware/voice/run.sh"

echo "== Screen gestures (dot_ui.c)"
"$cc" -O1 -w -I"$root/interface" "$root/tests/firmware/ui/controls.c" "$root/interface/dot_ui.c" -o "$tmp/controls"
"$tmp/controls"

echo "== Battery decoding (AXP2101)"
"$cc" -O1 -Wall -I"$root/firmware/main" "$root/tests/firmware/ui/BatteryTest.c" -o "$tmp/battery"
"$tmp/battery"

if command -v node >/dev/null 2>&1; then
    echo "== Bridge (Node 22.13+)"
    (cd "$root/bridge" && node --test tests/bridge.test.mjs | tail -8)
else
    echo "== Bridge tests skipped: Node.js 22.13+ not found"
fi

if [ -n "${JAVA_HOME:-}" ]; then bin="$JAVA_HOME/bin/"; else bin=""; fi
if ! "${bin}javac" -version >/dev/null 2>&1; then
    echo "== Android tests skipped: JDK 17+ not found (set JAVA_HOME)"
    exit 0
fi
echo "== Android contracts (Java)"
python3 "$root/android/cloud-dependencies.py" "$tmp/jars" >/dev/null
json="$tmp/jars/json-20250517.jar"
curl -fsSL --proto '=https' -o "$json" https://repo.maven.apache.org/maven2/org/json/json/20250517/json-20250517.jar
echo "3ea61b2a06e31edf1c91134fe9106b0ebb16628be169f3db75bc7a2b06b45796  $json" | shasum -a 256 -c - >/dev/null
cp="$json:$tmp/jars/msgpack-core-0.9.10.jar:$tmp/jars/Java-WebSocket-1.6.0.jar:$tmp/jars/slf4j-api-2.0.16.jar:$tmp/jars/slf4j-nop-2.0.16.jar"
src="$root/android/src/br/bolso/dot"
"${bin}javac" -nowarn -encoding UTF-8 -cp "$cp" -d "$tmp/classes" \
    "$src/CloudVoice.java" "$src/PcmStream.java" "$src/SpeechTransfer.java" "$src/VoiceBuffer.java" \
    "$src/Timings.java" "$src/SpeechAudio.java" "$src/BatteryInfo.java" "$src/NoticeReceipt.java" \
    "$src/PollSchedule.java" "$src/TraceCorrelation.java" "$root"/tests/android/*.java
for t in VoiceContracts ProtocolTest LatencyTest StartTest; do
    "${bin}java" -cp "$tmp/classes:$cp" "br.bolso.dot.$t"
done
echo "All tests passed."
