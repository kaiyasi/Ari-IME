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

# Exercise the runtime verification without requiring a desktop Fcitx5 daemon.
# The helper must reload an existing daemon, start an absent one in a graphical
# session, and fail when selecting the addon does not actually succeed.
fake_bin="$test_root/fake-bin"
fake_state="$test_root/fake-fcitx-state"
mkdir -p -- "$fake_bin" "$fake_state"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'case "${1:-}" in' \
    '  --check) test -f "$FAKE_FCITX_STATE/ready" ;;' \
    '  -r) touch "$FAKE_FCITX_STATE/ready" ;;' \
    '  -s) [[ "${2:-}" == inputer && "${INPUTER_FAKE_SELECT_FAIL:-0}" != 1 ]] && printf "%s\\n" inputer > "$FAKE_FCITX_STATE/active" ;;' \
    '  -n) test -f "$FAKE_FCITX_STATE/active" && cat "$FAKE_FCITX_STATE/active" ;;' \
    '  *) exit 1 ;;' \
    'esac' >"$fake_bin/fcitx5-remote"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    '[[ "${1:-}" == -d ]] && touch "$FAKE_FCITX_STATE/ready"' \
    >"$fake_bin/fcitx5"
chmod +x -- "$fake_bin/fcitx5-remote" "$fake_bin/fcitx5"

touch -- "$fake_state/ready"
PATH="$fake_bin:$PATH" FAKE_FCITX_STATE="$fake_state" DISPLAY=:99 \
    INPUTER_FCITX_PROFILE="$existing" bash "$helper" \
    --yes --make-default >"$test_root/reload.log"
grep -q '^Ari IME is active (inputer)$' "$test_root/reload.log"

rm -- "$fake_state/ready"
PATH="$fake_bin:$PATH" FAKE_FCITX_STATE="$fake_state" DISPLAY=:99 \
    INPUTER_FCITX_PROFILE="$existing" bash "$helper" \
    --yes --make-default >"$test_root/start.log"
grep -q '^Starting Fcitx5$' "$test_root/start.log"
grep -q '^Ari IME is active (inputer)$' "$test_root/start.log"

if PATH="$fake_bin:$PATH" FAKE_FCITX_STATE="$fake_state" DISPLAY=:99 \
    INPUTER_FAKE_SELECT_FAIL=1 INPUTER_FCITX_PROFILE="$existing" \
    bash "$helper" --yes --make-default >/dev/null 2>&1; then
    printf '%s\n' 'Expected selection failure to return non-zero' >&2
    exit 1
fi

printf '%s\n' 'enable-input-method profile tests passed'
