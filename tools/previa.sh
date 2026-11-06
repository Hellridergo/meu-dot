#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?Specify the HTML output path}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${WASM_CC:-clang}" -target wasm32 -O2 -ffreestanding -fno-builtin -DDOT_PREVIEW -c "$root/interface/dot_ui.c" -o "$tmp/ui.o"
# wasm-ld is often missing from PATH; the ESP-IDF esp-clang toolchain ships one.
if [[ -z "${WASM_LD:-}" ]] && ! command -v wasm-ld >/dev/null; then
  WASM_LD="$(ls -d "$HOME"/Desktop/ESP32DOT/.embuild/espressif/tools/esp-clang/*/esp-clang/bin/wasm-ld 2>/dev/null | tail -1)"
fi
"${WASM_LD:-wasm-ld}" --no-entry --export-memory --export-all --strip-all "$tmp/ui.o" -o "$tmp/ui.wasm"
python3 - "$root/interface/previa.in.html" "$tmp/ui.wasm" "$out" <<'PY'
import base64,sys
from pathlib import Path
template,wasm,out=map(Path,sys.argv[1:])
out.write_text(template.read_text().replace('__WASM64__',base64.b64encode(wasm.read_bytes()).decode()))
PY
