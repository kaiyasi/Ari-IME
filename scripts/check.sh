#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

build_dir="${INPUTER_BUILD_DIR:-build}"
sanitize_dir="${INPUTER_SANITIZE_BUILD_DIR:-build-sanitize}"
install_prefix="${INPUTER_INSTALL_PREFIX:-/tmp/inputer-install-check}"
mode="${INPUTER_CHECK_MODE:-all}"

run() {
    printf '\n==> %s\n' "$*"
    "$@"
}

extract_cmake_version() {
    sed -n 's/^project(inputer VERSION \([^ ]*\) LANGUAGES CXX).*/\1/p' CMakeLists.txt
}

extract_pkgbuild_version() {
    sed -n 's/^pkgver=\(.*\)$/\1/p' PKGBUILD
}

extract_srcinfo_version() {
    sed -n 's/^[[:space:]]*pkgver = \(.*\)$/\1/p' .SRCINFO | head -n1
}

check_versions() {
    local cmake_version pkgbuild_version srcinfo_version
    cmake_version="$(extract_cmake_version)"
    pkgbuild_version="$(extract_pkgbuild_version)"
    srcinfo_version="$(extract_srcinfo_version)"

    if [[ -z "$cmake_version" || -z "$pkgbuild_version" || -z "$srcinfo_version" ]]; then
        printf 'Failed to read version from CMakeLists.txt, PKGBUILD, or .SRCINFO\n' >&2
        exit 1
    fi
    if [[ "$cmake_version" != "$pkgbuild_version" ||
          "$cmake_version" != "$srcinfo_version" ]]; then
        printf 'Version mismatch: CMake=%s PKGBUILD=%s .SRCINFO=%s\n' \
            "$cmake_version" "$pkgbuild_version" "$srcinfo_version" >&2
        exit 1
    fi
}

check_srcinfo() {
    if ! command -v makepkg >/dev/null 2>&1; then
        return
    fi
    if [[ -f .SRCINFO ]]; then
        local srcinfo_tmp
        srcinfo_tmp="$(mktemp /tmp/inputer-srcinfo-XXXXXX)"
        makepkg --printsrcinfo >"$srcinfo_tmp"
        set +e
        run diff -u .SRCINFO "$srcinfo_tmp"
        local status=$?
        set -e
        rm -f "$srcinfo_tmp"
        if [[ "$status" -ne 0 ]]; then
            exit "$status"
        fi
    else
        run makepkg --printsrcinfo
    fi
}

release_checks() {
    check_versions
    run cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    run cmake --build "$build_dir"
    run ctest --test-dir "$build_dir" -j"${INPUTER_TEST_JOBS:-2}" --output-on-failure
    run cmake --install "$build_dir" --prefix "$install_prefix"
    run bash -n PKGBUILD
    check_srcinfo
}

sanitize_checks() {
    run cmake -S . -B "$sanitize_dir" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DINPUTER_ENABLE_SANITIZERS=ON \
        -DBUILD_TESTING=ON
    run cmake --build "$sanitize_dir"
    run ctest --test-dir "$sanitize_dir" -j"${INPUTER_TEST_JOBS:-2}" --output-on-failure
}

package_checks() {
    check_versions
    local pkgbuild_version
    pkgbuild_version="$(extract_pkgbuild_version)"
    tmp="$(mktemp -d /tmp/inputer-pkgcheck-XXXXXX)"
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/Ari-IME-$pkgbuild_version"
    cp -a CMakeLists.txt LICENSE README.md data src test \
        "$tmp/Ari-IME-$pkgbuild_version/"
    run env srcdir="$tmp" pkgdir="$tmp/pkg" bash -lc \
        'source PKGBUILD; build; check; package; find "$pkgdir" -type f | sort'
}

case "$mode" in
all)
    release_checks
    sanitize_checks
    if [[ "${INPUTER_CHECK_PACKAGE:-0}" == "1" ]]; then
        package_checks
    fi
    ;;
release)
    release_checks
    ;;
sanitize)
    sanitize_checks
    ;;
package)
    check_srcinfo
    package_checks
    ;;
*)
    printf 'Unknown INPUTER_CHECK_MODE: %s\n' "$mode" >&2
    exit 2
    ;;
esac
