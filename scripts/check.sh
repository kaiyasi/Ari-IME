#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

build_dir="${INPUTER_BUILD_DIR:-build}"
sanitize_dir="${INPUTER_SANITIZE_BUILD_DIR:-build-sanitize}"
fuzz_dir="${INPUTER_FUZZ_BUILD_DIR:-build-fuzz}"
fuzz_corpus_dir="${INPUTER_FUZZ_CORPUS_DIR:-test/corpus/fuzz_buffer}"
coverage_dir="${INPUTER_COVERAGE_BUILD_DIR:-build-coverage}"
install_prefix="${INPUTER_INSTALL_PREFIX:-/tmp/inputer-install-check}"
mode="${INPUTER_CHECK_MODE:-all}"
build_type="${INPUTER_BUILD_TYPE:-Release}"

run() {
    printf '\n==> %s\n' "$*"
    "$@"
}

cmake_compiler_args() {
    if [[ -n "${INPUTER_CC:-}" ]]; then
        printf '%s\n' "-DCMAKE_C_COMPILER=$INPUTER_CC"
    fi
    if [[ -n "${INPUTER_CXX:-}" ]]; then
        printf '%s\n' "-DCMAKE_CXX_COMPILER=$INPUTER_CXX"
    fi
    if [[ -n "${INPUTER_CXX_COMPILER_LAUNCHER:-}" ]]; then
        printf '%s\n' "-DCMAKE_CXX_COMPILER_LAUNCHER=$INPUTER_CXX_COMPILER_LAUNCHER"
    fi
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
    printf 'Ari IME version: v%s\n' "$cmake_version"
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
    local cmake_args=()
    mapfile -t cmake_args < <(cmake_compiler_args)
    run cmake -S . -B "$build_dir" "${cmake_args[@]}" \
        -DCMAKE_BUILD_TYPE="$build_type" -DBUILD_TESTING=ON
    run cmake --build "$build_dir"
    run ctest --test-dir "$build_dir" -j"${INPUTER_TEST_JOBS:-2}" --output-on-failure
    run cmake --install "$build_dir" --prefix "$install_prefix"
    run bash -n PKGBUILD
    check_srcinfo
}

sanitize_checks() {
    local cmake_args=()
    mapfile -t cmake_args < <(cmake_compiler_args)
    run cmake -S . -B "$sanitize_dir" \
        "${cmake_args[@]}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DINPUTER_ENABLE_SANITIZERS=ON \
        -DBUILD_TESTING=ON
    run cmake --build "$sanitize_dir"
    run ctest --test-dir "$sanitize_dir" -j"${INPUTER_TEST_JOBS:-2}" --output-on-failure
}

fuzz_checks() {
    if ! command -v clang++ >/dev/null 2>&1; then
        if [[ "${INPUTER_FUZZ_ALLOW_SKIP:-0}" == "1" ]]; then
            printf 'Skipping fuzz checks: clang++ not found\n'
            return
        fi
        printf 'clang++ is required for INPUTER_CHECK_MODE=fuzz\n' >&2
        exit 1
    fi

    local cmake_args=()
    mapfile -t cmake_args < <(cmake_compiler_args)
    run cmake -S . -B "$fuzz_dir" \
        "${cmake_args[@]}" \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_BUILD_TYPE=Debug \
        -DINPUTER_ENABLE_FUZZING=ON \
        -DBUILD_TESTING=OFF
    run cmake --build "$fuzz_dir" --target fuzz_buffer
    local fuzz_args=("-runs=${INPUTER_FUZZ_RUNS:-256}")
    if [[ -d "$fuzz_corpus_dir" ]]; then
        fuzz_args+=("$fuzz_corpus_dir")
    fi
    run env ASAN_OPTIONS=detect_leaks=0 LSAN_OPTIONS=detect_leaks=0 \
        "$fuzz_dir/fuzz_buffer" "${fuzz_args[@]}"
}

coverage_checks() {
    if ! command -v gcov >/dev/null 2>&1; then
        printf 'gcov is required for INPUTER_CHECK_MODE=coverage\n' >&2
        exit 1
    fi

    local cmake_args=()
    mapfile -t cmake_args < <(cmake_compiler_args)
    run cmake -S . -B "$coverage_dir" \
        "${cmake_args[@]}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DINPUTER_ENABLE_COVERAGE=ON \
        -DBUILD_TESTING=ON
    run cmake --build "$coverage_dir"
    run ctest --test-dir "$coverage_dir" -j"${INPUTER_TEST_JOBS:-2}" --output-on-failure

    local report_dir="$coverage_dir/gcov"
    mkdir -p "$report_dir"
    find "$report_dir" -maxdepth 1 -name '*.gcov' -delete
    run gcov -b -c \
        "$coverage_dir/CMakeFiles/test_buffer.dir/src/buffer.cpp.gcda" \
        "$coverage_dir/CMakeFiles/test_buffer.dir/src/zhuyin.cpp.gcda"
    mv ./*.gcov "$report_dir"/
    run gcov -b -c \
        "$coverage_dir/CMakeFiles/test_layout.dir/src/layout.cpp.gcda"
    mv ./*.gcov "$report_dir"/
    printf '\nCoverage reports written to %s\n' "$report_dir"
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
    run env srcdir="$tmp" pkgdir="$tmp/pkg" bash -e -o pipefail -lc \
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
fuzz)
    fuzz_checks
    ;;
coverage)
    coverage_checks
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
