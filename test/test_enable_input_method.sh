#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
helper="$script_dir/scripts/enable-input-method.sh"
test_root="$(mktemp -d /tmp/inputer-profile-ctest-XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT

assert_count() {
    local expected="$1"
    local pattern="$2"
    local file="$3"
    local actual
    actual="$(grep -c -- "$pattern" "$file" || true)"
    if [[ "$actual" -ne "$expected" ]]; then
        printf 'Expected %s matches for %s in %s, got %s\n' \
            "$expected" "$pattern" "$file" "$actual" >&2
        exit 1
    fi
}

fresh="$test_root/fresh-profile"
INPUTER_FCITX_PROFILE="$fresh" bash "$helper" \
    --yes --no-restart --make-default >/dev/null
assert_count 1 '^Name=inputer$' "$fresh"
assert_count 1 '^DefaultIM=inputer$' "$fresh"
grep -q '^\[Groups/0/Items/0\]$' "$fresh"
grep -q '^\[GroupOrder\]$' "$fresh"
grep -q '^0=Default$' "$fresh"

# The operation is idempotent and must not append a second Ari entry.
INPUTER_FCITX_PROFILE="$fresh" bash "$helper" \
    --yes --no-restart --make-default >/dev/null
assert_count 1 '^Name=inputer$' "$fresh"

existing="$test_root/existing-profile"
printf '%s\n' \
    '[Groups/0]' \
    'Name=Default' \
    'Default Layout=us' \
    'DefaultIM=keyboard-us' \
    '' \
    '[Groups/0/Items/0]' \
    'Name=keyboard-us' \
    'Layout=' \
    '' \
    '[Groups/0/Items/1]' \
    'Name=chewing' \
    'Layout=' >"$existing"

# Adding Ari without --make-default preserves the existing default and items.
INPUTER_FCITX_PROFILE="$existing" bash "$helper" \
    --yes --no-restart >/dev/null
assert_count 1 '^Name=inputer$' "$existing"
grep -q '^DefaultIM=keyboard-us$' "$existing"
grep -q '^\[Groups/0/Items/2\]$' "$existing"
backup="$(find "$test_root" -maxdepth 1 -name 'existing-profile.bak.*' -print -quit)"
test -n "$backup"

ordered="$test_root/ordered-profile"
printf '%s\n' \
    '[Groups/0]' \
    'Name=Default' \
    'Default Layout=us' \
    'DefaultIM=keyboard-us' \
    '' \
    '[Groups/0/Items/0]' \
    'Name=keyboard-us' \
    'Layout=' \
    '' \
    '[GroupOrder]' \
    '0=Other' >"$ordered"
INPUTER_FCITX_PROFILE="$ordered" bash "$helper" \
    --yes --no-restart >/dev/null
grep -q '^0=Other$' "$ordered"
grep -q '^1=Default$' "$ordered"

INPUTER_FCITX_PROFILE="$existing" bash "$helper" \
    --yes --no-restart --make-default >/dev/null
assert_count 1 '^DefaultIM=inputer$' "$existing"

before="$test_root/dry-before"
cp -- "$existing" "$before"
INPUTER_FCITX_PROFILE="$existing" bash "$helper" \
    --dry-run --make-default >/dev/null
cmp -- "$before" "$existing"

printf '%s\n' 'enable-input-method profile tests passed'
