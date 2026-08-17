#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
build_dir="${ARI_WASM_BUILD_DIR:-$repo_root/build-wasm}"
include_dir="${CHEWING_WASM_INCLUDE_DIR:-}"
library="${CHEWING_WASM_LIBRARY:-}"
data_dir="${CHEWING_DATA_DIR:-}"

command -v emcmake >/dev/null 2>&1 || {
    printf 'emcmake/em++ is required; install and activate Emscripten first\n' >&2
    exit 2
}
if [[ -z "$include_dir" || -z "$library" || -z "$data_dir" ]]; then
    printf 'Set CHEWING_WASM_INCLUDE_DIR, CHEWING_WASM_LIBRARY, and CHEWING_DATA_DIR\n' >&2
    exit 2
fi

emcmake cmake -S "$script_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCHEWING_INCLUDE_DIR="$include_dir" \
    -DCHEWING_LIBRARY="$library" \
    -DCHEWING_DATA_DIR="$data_dir"
cmake --build "$build_dir" --target ari-ime-wasm -j2

dist_dir="${ARI_WASM_DIST_DIR:-$script_dir/dist}"
mkdir -p "$dist_dir"
cp "$build_dir/ari-ime-wasm.js" "$dist_dir/ari-ime-wasm.js"
cp "$build_dir/ari-ime-wasm.wasm" "$dist_dir/ari-ime-wasm.wasm"
if [[ -f "$build_dir/ari-ime-wasm.data" ]]; then
    cp "$build_dir/ari-ime-wasm.data" "$dist_dir/ari-ime-wasm.data"
fi
printf 'WASM package artifacts written to %s\n' "$dist_dir"
