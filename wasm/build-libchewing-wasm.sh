#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="${LIBCHEWING_SOURCE_DIR:-}"
work_dir="${ARI_LIBCHEWING_BUILD_DIR:-/tmp/ari-libchewing-wasm-build}"
native_build="$work_dir/native"
native_prefix="$work_dir/native-install"
wasm_build="$work_dir/wasm"
wasm_prefix="$work_dir/wasm-install"

command -v cmake >/dev/null 2>&1 || {
    printf 'cmake is required\n' >&2
    exit 2
}
command -v emcmake >/dev/null 2>&1 || {
    printf 'emcmake/em++ is required; install and activate Emscripten first\n' >&2
    exit 2
}
command -v patch >/dev/null 2>&1 || {
    printf 'patch is required\n' >&2
    exit 2
}
if [[ -z "$source_dir" || ! -f "$source_dir/CMakeLists.txt" ]]; then
    printf 'Set LIBCHEWING_SOURCE_DIR to a libchewing source checkout (v0.8.5)\n' >&2
    exit 2
fi

mkdir -p "$work_dir"
staged_source="$(mktemp -d "$work_dir/source.XXXXXX")"
cp -a "$source_dir"/. "$staged_source"/

# libchewing 0.8's C backend uses POSIX mmap for its dictionary. MEMFS has no
# usable mmap implementation for this access pattern, so the checked-in patch
# makes the two read-only maps ordinary malloc/read buffers for Emscripten.
patch -d "$staged_source" -p1 < \
    "$script_dir/patches/libchewing-0.8.5-emscripten.patch"

cmake -S "$staged_source" -B "$native_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWITH_RUST=OFF \
    -DWITH_SQLITE3=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX="$native_prefix"
cmake --build "$native_build" --parallel 2
cmake --install "$native_build"

# The cross build cannot execute its generated init_database.js while making
# the dictionary. Generate the legacy data natively once, then seed the cross
# build's output directory before its normal target checks run.
emcmake cmake -S "$staged_source" -B "$wasm_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWITH_RUST=OFF \
    -DWITH_SQLITE3=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX="$wasm_prefix"
mkdir -p "$wasm_build/data"
cmake --build "$wasm_build" --target init_database dump_database --parallel 2
cp "$native_build/data/dictionary.dat" "$wasm_build/data/dictionary.dat"
cp "$native_build/data/index_tree.dat" "$wasm_build/data/index_tree.dat"
cmake --build "$wasm_build" --target data --parallel 2
cmake --build "$wasm_build" --target libchewing --parallel 2
cmake --install "$wasm_build"

printf '\nUse these values with build-wasm.sh:\n'
printf 'CHEWING_WASM_INCLUDE_DIR=%s\n' "$wasm_prefix/include/chewing"
printf 'CHEWING_WASM_LIBRARY=%s\n' "$wasm_prefix/lib/libchewing.a"
printf 'CHEWING_DATA_DIR=%s\n' "$wasm_prefix/share/libchewing"
