#!/usr/bin/env bash
set -euo pipefail

dict_tool="${1:?path to ari-ime-dict is required}"
test_root="$(mktemp -d /tmp/inputer-dict-ctest-XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT

export INPUTER_USER_DATA_DIR="$test_root/inputer"
export HOME="$test_root/home"
export XDG_CONFIG_HOME="$test_root/config"
export XDG_DATA_HOME="$test_root/data"
export CHEWING_USER_PATH="$test_root/chewing"

"$dict_tool" info >"$test_root/info"
grep -q $'^entries\t0$' "$test_root/info"

printf '%s\n' \
    '# Ari IME user dictionary v1' \
    '# A canonical Unicode Bopomofo reading is portable across layouts.' \
    $'妳\tㄋㄧˇ' >"$test_root/import.tsv"

"$dict_tool" import --dry-run "$test_root/import.tsv" >"$test_root/dry-run"
grep -q '^validated 1 entry$' "$test_root/dry-run"
"$dict_tool" info >"$test_root/info-after-dry-run"
grep -q $'^entries\t0$' "$test_root/info-after-dry-run"

"$dict_tool" import "$test_root/import.tsv" >"$test_root/import-result"
grep -q '^added 1, skipped 0' "$test_root/import-result"
"$dict_tool" info >"$test_root/info-after-import"
grep -q $'^entries\t1$' "$test_root/info-after-import"
"$dict_tool" export "$test_root/export.tsv"
grep -Fq $'妳\tㄋㄧˇ' "$test_root/export.tsv"
"$dict_tool" candidates su3 >"$test_root/candidates"
grep -q $'^preedit\t妳$' "$test_root/candidates"
grep -q $'^candidate\t妳$' "$test_root/candidates"

# A second merge is idempotent and still keeps a backup of the current data.
"$dict_tool" import "$test_root/import.tsv" >"$test_root/second-import"
grep -q '^added 0, skipped 1' "$test_root/second-import"
backup_path="$(sed -n 's/.*backup //p' "$test_root/second-import")"
test -n "$backup_path"
test -f "$backup_path/chewing.dat"

"$dict_tool" backup >"$test_root/backup"
backup_path="$(cat "$test_root/backup")"
test -d "$backup_path"
test -f "$backup_path/chewing.dat"

printf '%s\n' '# Ari IME user dictionary v1' 'not-a-tab-separated-entry' \
    >"$test_root/invalid.tsv"
if "$dict_tool" import "$test_root/invalid.tsv" \
    >"$test_root/invalid-out" 2>"$test_root/invalid-err"; then
    printf 'invalid dictionary unexpectedly imported\n' >&2
    exit 1
fi
grep -q 'invalid import' "$test_root/invalid-err"

printf '%s\n' 'dictionary tool tests passed'
