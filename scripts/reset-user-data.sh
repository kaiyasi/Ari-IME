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

# libchewing 0.12 keeps its learned dictionary at CHEWING_USER_PATH, falling back
# to $XDG_DATA_HOME/chewing (then $HOME/.local/share/chewing). Ari now pins
# CHEWING_USER_PATH to its own data directory, so fresh learning lands there; this
# resolves the *shared* location where pre-fix (or other-IME) learning may sit.
resolve_shared_chewing_dir() {
    if [[ -n "${CHEWING_USER_PATH:-}" ]]; then
        printf '%s\n' "$CHEWING_USER_PATH"
        return
    fi
    if [[ -n "${XDG_DATA_HOME:-}" ]]; then
        printf '%s/chewing\n' "$XDG_DATA_HOME"
        return
    fi
    if [[ -n "${HOME:-}" ]]; then
        printf '%s/.local/share/chewing\n' "$HOME"
        return
    fi
    printf '\n'
}

usage() {
    cat <<'EOF'
Usage: scripts/reset-user-data.sh [--yes] [--no-backup] [--include-shared]

Resets Ari IME's learned data: userdict.dat plus libchewing's learned files
(chewing.dat, chewing-deleted.dat) in Ari's own data directory. Built-in
libchewing dictionary resources are not touched.

libchewing 0.12 stored learning in a SHARED directory ($XDG_DATA_HOME/chewing)
before Ari pinned it to its own directory. That shared data may also be used by
other libchewing input methods (fcitx5-chewing, ibus-chewing), so it is left
alone unless you pass --include-shared.

Options:
  --yes             do not prompt for confirmation
  --no-backup       delete instead of backing up to .bak.<timestamp>
  --include-shared  ALSO reset the shared chewing directory (affects any other
                    libchewing input method that shares it)

Environment overrides:
  INPUTER_USER_DATA_DIR  exact user-data directory to reset
  XDG_CONFIG_HOME        standard config root (uses $XDG_CONFIG_HOME/inputer)
  CHEWING_USER_PATH      explicit libchewing learned-dictionary directory
EOF
}

confirm=0
backup=1
include_shared=0
while [[ $# -gt 0 ]]; do
    case "$1" in
    --yes) confirm=1 ;;
    --no-backup) backup=0 ;;
    --include-shared) include_shared=1 ;;
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
shared_dir="$(resolve_shared_chewing_dir)"

# Ari-owned files (Ari's own directory).
targets=(
    "$user_data_dir/userdict.dat"
    "$user_data_dir/chewing.dat"
    "$user_data_dir/chewing-deleted.dat"
)
# Shared chewing files, only when explicitly requested and not Ari's own dir.
if [[ "$include_shared" -eq 1 && -n "$shared_dir" &&
      "$shared_dir" != "$user_data_dir" ]]; then
    targets+=(
        "$shared_dir/chewing.dat"
        "$shared_dir/chewing-deleted.dat"
    )
fi

existing=()
for t in "${targets[@]}"; do
    [[ -e "$t" ]] && existing+=("$t")
done

# Notice when shared data exists but was not selected for reset.
if [[ "$include_shared" -ne 1 && -n "$shared_dir" &&
      "$shared_dir" != "$user_data_dir" ]]; then
    if [[ -e "$shared_dir/chewing.dat" || -e "$shared_dir/chewing-deleted.dat" ]]; then
        printf 'Note: shared libchewing learning exists in %s\n' "$shared_dir"
        printf '      (possibly used by other chewing input methods). Pass --include-shared to reset it too.\n'
    fi
fi

if [[ "${#existing[@]}" -eq 0 ]]; then
    printf 'No learned Ari IME data found under %s\n' "$user_data_dir"
    exit 0
fi

if [[ "$confirm" -ne 1 ]]; then
    printf 'About to reset the following Ari IME learned data:\n'
    for t in "${existing[@]}"; do
        printf '  %s\n' "$t"
    done
    printf 'This keeps built-in dictionaries intact. Continue? [y/N] '
    read -r answer
    case "$answer" in
    y|Y|yes|YES) ;;
    *)
        printf 'Aborted.\n'
        exit 1
        ;;
    esac
fi

stamp="$(date +%Y%m%d-%H%M%S)"
for t in "${existing[@]}"; do
    if [[ "$backup" -eq 1 ]]; then
        backup_path="$t.bak.$stamp"
        mv "$t" "$backup_path"
        printf 'Backed up %s -> %s\n' "$t" "$backup_path"
    else
        rm -f "$t"
        printf 'Removed %s\n' "$t"
    fi
done
