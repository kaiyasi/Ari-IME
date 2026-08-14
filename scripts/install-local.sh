#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_input="${INPUTER_BUILD_DIR:-build-public-release}"

if [[ "$build_input" = /* ]]; then
    build_dir="$build_input"
else
    build_dir="$repo_root/$build_input"
fi

if [[ -n "${INPUTER_INSTALL_PREFIX:-}" ]]; then
    install_prefix="$INPUTER_INSTALL_PREFIX"
elif [[ -n "${HOME:-}" ]]; then
    install_prefix="$HOME/.local"
else
    printf 'INPUTER_INSTALL_PREFIX or HOME is required\n' >&2
    exit 2
fi

if ! command -v cmake >/dev/null 2>&1; then
    printf 'cmake is required\n' >&2
    exit 2
fi
if ! command -v fcitx5 >/dev/null 2>&1 ||
   ! command -v fcitx5-remote >/dev/null 2>&1; then
    printf 'fcitx5 and fcitx5-remote are required\n' >&2
    exit 2
fi
if [[ ! -f "$build_dir/cmake_install.cmake" ]]; then
    printf 'Build directory is not configured: %s\n' "$build_dir" >&2
    printf 'Run cmake -B %s -DCMAKE_BUILD_TYPE=Release first\n' "$build_dir" >&2
    exit 2
fi

printf 'Installing Ari IME into %s\n' "$install_prefix"
cmake --install "$build_dir" --prefix "$install_prefix"

local_addon_dir="$install_prefix/lib/fcitx5"
addon_paths=("$local_addon_dir")
for system_addon_dir in \
    /usr/lib/fcitx5 \
    /usr/lib/x86_64-linux-gnu/fcitx5 \
    /usr/lib/aarch64-linux-gnu/fcitx5; do
    if [[ -d "$system_addon_dir" && "$system_addon_dir" != "$local_addon_dir" ]]; then
        addon_paths+=("$system_addon_dir")
    fi
done
old_ifs="$IFS"
IFS=:
addon_path_env="${addon_paths[*]}"
IFS="$old_ifs"

if fcitx5-remote --check >/dev/null 2>&1; then
    printf 'Stopping the current Fcitx5 daemon\n'
    fcitx5-remote -e
    for _ in {1..50}; do
        if ! fcitx5-remote --check >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
fi

printf 'Starting Fcitx5 with FCITX_ADDON_DIRS=%s\n' "$addon_path_env"
FCITX_ADDON_DIRS="$addon_path_env" fcitx5 -d
for _ in {1..50}; do
    if fcitx5-remote --check >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
if ! fcitx5-remote --check >/dev/null 2>&1; then
    printf 'Fcitx5 did not become ready after restart\n' >&2
    exit 1
fi

fcitx5-remote -s inputer
current_im="$(fcitx5-remote -n)"
if [[ "$current_im" != inputer ]]; then
    printf 'Failed to select inputer; current input method: %s\n' "$current_im" >&2
    exit 1
fi

fcitx_pid="$(pgrep -xo fcitx5 || true)"
if [[ -z "$fcitx_pid" || ! -r "/proc/$fcitx_pid/maps" ]]; then
    printf 'Cannot inspect the running Fcitx5 module path\n' >&2
    exit 1
fi
if ! grep -Fq -- "$local_addon_dir/inputer.so" "/proc/$fcitx_pid/maps"; then
    printf 'Fcitx5 did not load the local module: %s/inputer.so\n' "$local_addon_dir" >&2
    printf 'Check that the selected input method is Ari IME and retry\n' >&2
    exit 1
fi

printf 'Fcitx5 is using the local Ari IME module (%s)\n' "$local_addon_dir/inputer.so"
