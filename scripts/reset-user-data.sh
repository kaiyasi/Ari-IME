#!/usr/bin/env bash
set -euo pipefail

resolve_user_data_dir() {
    if [[ -n "${INPUTER_USER_DATA_DIR:-}" ]]; then
        printf '%s\n' "$INPUTER_USER_DATA_DIR"
        return
    fi
    if [[ -n "${XDG_CONFIG_HOME:-}" ]]; then
        printf '%s/inputer\n' "$XDG_CONFIG_HOME"
        return
    fi
    if [[ -n "${HOME:-}" ]]; then
        printf '%s/.config/inputer\n' "$HOME"
        return
    fi
    printf 'Could not resolve a user data directory\n' >&2
    exit 2
}

usage() {
    cat <<'EOF'
Usage: scripts/reset-user-data.sh [--yes] [--no-backup]

Backs up Ari IME's learned user dictionary and removes the active userdict file.
This resets only per-user learned phrase/homophone data. Built-in libchewing
dictionary resources are not touched.

Environment overrides:
  INPUTER_USER_DATA_DIR  exact user-data directory to reset
  XDG_CONFIG_HOME        standard config root (uses $XDG_CONFIG_HOME/inputer)
EOF
}

confirm=0
backup=1
while [[ $# -gt 0 ]]; do
    case "$1" in
    --yes)
        confirm=1
        ;;
    --no-backup)
        backup=0
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        printf 'Unknown option: %s\n' "$1" >&2
        usage >&2
        exit 2
        ;;
    esac
    shift
done

user_data_dir="$(resolve_user_data_dir)"
dict_path="$user_data_dir/userdict.dat"

if [[ ! -e "$dict_path" ]]; then
    printf 'No learned user dictionary found at %s\n' "$dict_path"
    exit 0
fi

if [[ "$confirm" -ne 1 ]]; then
    printf 'About to reset Ari IME learned data: %s\n' "$dict_path"
    printf 'This keeps built-in dictionaries intact. Continue? [y/N] '
    read -r answer
    case "$answer" in
    y|Y|yes|YES)
        ;;
    *)
        printf 'Aborted.\n'
        exit 1
        ;;
    esac
fi

mkdir -p "$user_data_dir"

if [[ "$backup" -eq 1 ]]; then
    backup_path="$dict_path.bak.$(date +%Y%m%d-%H%M%S)"
    mv "$dict_path" "$backup_path"
    printf 'Backed up learned dictionary to %s\n' "$backup_path"
else
    rm -f "$dict_path"
    printf 'Removed learned dictionary %s\n' "$dict_path"
fi
