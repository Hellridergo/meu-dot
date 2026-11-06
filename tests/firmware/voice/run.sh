#!/bin/sh
# Compiles the real firmware/main/voice.c on the host with AddressSanitizer and
# UBSan, against stub codec, GATT, queue, mutex, clock and scheduler. The
# microphone uplink task runs as a real thread.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/meu-dot-voice.XXXXXX")
trap 'rm -rf "$build"' EXIT
mkdir -p "$build/bsp" "$build/freertos"
for header in bsp/esp-bsp.h esp_codec_dev.h esp_heap_caps.h esp_log.h esp_random.h esp_timer.h freertos/FreeRTOS.h freertos/queue.h freertos/semphr.h freertos/task.h; do
    printf '%s\n' '#include "host_stubs.h"' > "$build/$header"
done
"${CC:-cc}" -std=c11 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -pthread \
    -I"$here" -I"$build" -I"$root/interface" -I"$root/firmware/main" \
    "$here/harness.c" "$root/firmware/main/voice.c" -o "$build/test"
"$build/test" "$@"
