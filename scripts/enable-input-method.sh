#!/usr/bin/env bash
set -euo pipefail

# Add Ari IME to the user's Fcitx5 profile without touching the package
# installation or any other input-method entry. This is deliberately an
# explicit command: distribution package installation must not silently change
# the user's active input method.

usage() {
    cat <<'EOF'
Usage: ari-ime-enable [options]

Add Ari IME to the first Fcitx5 input-method group. The profile is backed up
before it is changed. By default the current default input method is kept;
use --make-default when Ari should become the group's default.

Options:
  --make-default  make Ari IME the group's default input method
  --yes            do not ask for confirmation before changing the profile
  --dry-run        show the planned change without writing or restarting Fcitx5
  --no-restart     do not reload Fcitx5 after changing the profile
  --profile PATH   use PATH instead of the user's Fcitx5 profile
  -h, --help       show this help

Environment:
  INPUTER_FCITX_PROFILE  same as --profile
EOF
}

profile="${INPUTER_FCITX_PROFILE:-}"
make_default=0
assume_yes=0
dry_run=0
restart=1

while [[ $# -gt 0 ]]; do
    case "$1" in
    --make-default) make_default=1 ;;
    --yes) assume_yes=1 ;;
    --dry-run) dry_run=1 ;;
    --no-restart) restart=0 ;;
    --profile)
        if [[ $# -lt 2 ]]; then
            printf '%s\n' '--profile requires a path' >&2
            exit 2
        fi
        profile="$2"
        shift
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

if [[ -z "$profile" ]]; then
    config_home="${XDG_CONFIG_HOME:-${HOME:-}/.config}"
    if [[ -z "${HOME:-}" && -z "${XDG_CONFIG_HOME:-}" ]]; then
        printf 'HOME or XDG_CONFIG_HOME is required\n' >&2
        exit 2
    fi
    profile="$config_home/fcitx5/profile"
fi

if [[ -e "$profile" && ! -f "$profile" ]]; then
    printf 'Fcitx5 profile is not a regular file: %s\n' "$profile" >&2
    exit 1
fi

profile_dir="$(dirname -- "$profile")"
group_id=""
if [[ -s "$profile" ]]; then
    group_id="$(sed -n 's/^\[Groups\/\([0-9][0-9]*\)\]$/\1/p' "$profile" | head -n1)"
    if [[ -z "$group_id" ]]; then
        printf 'No Fcitx5 input-method group found in %s\n' "$profile" >&2
        exit 1
    fi
fi

if [[ -z "$group_id" ]]; then
    group_id=0
fi
group_section="Groups/$group_id"
group_name="Default"
if [[ -s "$profile" ]]; then
    group_name="$(awk -v section="[$group_section]" '
        $0 == section { in_group = 1; next }
        in_group && /^\[/ { in_group = 0 }
        in_group && /^Name=/ {
            sub(/^Name=/, "")
            print
            exit
        }
    ' "$profile")"
    if [[ -z "$group_name" ]]; then
        group_name="Default"
    fi
fi

inputer_present=0
if [[ -s "$profile" ]] && awk '
    /^\[Groups\/[0-9]+\/Items\/[0-9]+\]$/ { in_item = 1; next }
    /^\[/ { in_item = 0 }
    in_item && $0 == "Name=inputer" { found = 1 }
    END { exit(found ? 0 : 1) }
' "$profile"; then
    inputer_present=1
fi

next_item=0
if [[ -s "$profile" ]]; then
    next_item="$(sed -n \
        "s/^\[Groups\/${group_id}\/Items\/\([0-9][0-9]*\)\]$/\1/p" \
        "$profile" | awk 'BEGIN { max = -1 } $1 > max { max = $1 } END { print max + 1 }')"
elif [[ "$make_default" -eq 0 ]]; then
    # A new profile gets keyboard-us at item 0 below; append Ari at item 1.
    next_item=1
fi

set_default_needed=0
if [[ "$make_default" -eq 1 ]]; then
    if [[ -s "$profile" ]] && awk -v section="[$group_section]" '
        $0 == section { in_group = 1; next }
        in_group && /^\[/ { in_group = 0 }
        in_group && $0 == "DefaultIM=inputer" { found = 1 }
        END { exit(found ? 0 : 1) }
    ' "$profile"; then
        set_default_needed=0
    else
        set_default_needed=1
    fi
fi

group_order_needed=0
group_order_index=0
if [[ -s "$profile" ]] && awk -v group_name="$group_name" '
    $0 == "[GroupOrder]" { in_order = 1; next }
    in_order && /^\[/ { in_order = 0 }
    in_order && index($0, "=") > 0 &&
        substr($0, index($0, "=") + 1) == group_name { found = 1 }
    END { exit(found ? 0 : 1) }
' "$profile"; then
    group_order_needed=0
else
    group_order_needed=1
fi
if [[ -s "$profile" ]]; then
    group_order_index="$(awk '
        $0 == "[GroupOrder]" { in_order = 1; next }
        in_order && /^\[/ { in_order = 0 }
        in_order && index($0, "=") > 0 {
            key = substr($0, 1, index($0, "=") - 1)
            if (key ~ /^[0-9]+$/ && key + 1 > next_index) {
                next_index = key + 1
            }
        }
        END { print next_index + 0 }
    ' "$profile")"
fi

changed=0
if [[ "$inputer_present" -eq 0 || "$set_default_needed" -eq 1 ||
      "$group_order_needed" -eq 1 || ! -s "$profile" ]]; then
    changed=1
fi

if [[ "$inputer_present" -eq 1 ]]; then
    printf 'Ari IME is already present in %s\n' "$profile"
else
    printf 'Ari IME will be added to [%s/Items/%s]\n' "$group_section" "$next_item"
fi
if [[ "$make_default" -eq 1 ]]; then
    if [[ "$set_default_needed" -eq 1 || ! -s "$profile" ]]; then
        printf '%s\n' 'Ari IME will become the group default (--make-default).'
    else
        printf '%s\n' 'Ari IME is already the group default.'
    fi
fi
if [[ "$group_order_needed" -eq 1 ]]; then
    printf 'Fcitx5 group order will include %s.\n' "$group_name"
fi
printf 'Profile: %s\n' "$profile"

if [[ "$changed" -eq 0 ]]; then
    printf '%s\n' 'No profile changes are needed.'
else
    if [[ "$dry_run" -eq 1 ]]; then
        printf '%s\n' 'Dry run: no files or Fcitx5 state were changed.'
        exit 0
    fi

    if [[ "$assume_yes" -ne 1 ]]; then
        printf 'Continue and back up the profile before changing it? [y/N] '
        read -r answer
        case "$answer" in
        y|Y|yes|YES) ;;
        *)
            printf '%s\n' 'Aborted.'
            exit 1
            ;;
        esac
    fi

    mkdir -p -- "$profile_dir"
    temp_profile="$(mktemp "${profile}.tmp.XXXXXX")"
    trap 'rm -f -- "$temp_profile"' EXIT

    if [[ -s "$profile" ]]; then
        awk -v section="[$group_section]" \
            -v set_default="$set_default_needed" \
            -v ensure_order="$group_order_needed" \
            -v order_index="$group_order_index" \
            -v group_name="$group_name" '
            BEGIN {
                in_group = 0
                replaced = 0
                in_order = 0
                saw_order = 0
                order_found = 0
            }
            $0 == section {
                in_group = 1
                print
                next
            }
            in_group && /^\[/ {
                if (set_default == 1 && replaced == 0) {
                    print "DefaultIM=inputer"
                    replaced = 1
                }
                in_group = 0
            }
            $0 == "[GroupOrder]" {
                in_order = 1
                saw_order = 1
                print
                next
            }
            in_order && /^\[/ {
                if (ensure_order == 1 && order_found == 0) {
                    print order_index "=" group_name
                    order_found = 1
                }
                in_order = 0
            }
            in_order && index($0, "=") > 0 &&
                substr($0, index($0, "=") + 1) == group_name {
                order_found = 1
            }
            in_group && set_default == 1 && /^DefaultIM=/ {
                if (replaced == 0) {
                    print "DefaultIM=inputer"
                    replaced = 1
                }
                next
            }
            { print }
            END {
                if (in_group && set_default == 1 && replaced == 0) {
                    print "DefaultIM=inputer"
                }
                if (in_order && ensure_order == 1 && order_found == 0) {
                    print order_index "=" group_name
                }
                if (ensure_order == 1 && saw_order == 0) {
                    print ""
                    print "[GroupOrder]"
                    print order_index "=" group_name
                }
            }
        ' "$profile" >"$temp_profile"
    else
        default_im="keyboard-us"
        if [[ "$make_default" -eq 1 ]]; then
            default_im=inputer
        fi
        {
            printf '%s\n' '[Groups/0]'
            printf '%s\n' '# Group Name'
            printf '%s\n' 'Name=Default'
            printf '%s\n' '# Layout'
            printf '%s\n' 'Default Layout=us'
            printf 'DefaultIM=%s\n\n' "$default_im"
            if [[ "$make_default" -eq 0 ]]; then
                printf '%s\n' '[Groups/0/Items/0]'
                printf '%s\n' '# Name'
                printf '%s\n' 'Name=keyboard-us'
                printf '%s\n\n' '# Layout'
            fi
            printf '%s\n' '[GroupOrder]'
            printf '%s\n' '0=Default'
        } >"$temp_profile"
    fi

    if [[ "$inputer_present" -eq 0 ]]; then
        {
            printf '\n[Groups/%s/Items/%s]\n' "$group_id" "$next_item"
            printf '%s\n' '# Name'
            printf '%s\n' 'Name=inputer'
            printf '%s\n' '# Layout'
            printf '%s\n' 'Layout='
        } >>"$temp_profile"
    fi

    backup_path=""
    if [[ -e "$profile" ]]; then
        backup_path="$(mktemp "${profile}.bak.$(date +%Y%m%d-%H%M%S).XXXXXX")"
        cp -p -- "$profile" "$backup_path"
    fi
    chmod --reference="$profile" "$temp_profile" 2>/dev/null || true
    mv -- "$temp_profile" "$profile"
    trap - EXIT
    if [[ -n "$backup_path" ]]; then
        printf 'Backed up profile to %s\n' "$backup_path"
    else
        printf 'Created profile %s\n' "$profile"
    fi
fi

if [[ "$dry_run" -eq 1 || "$restart" -eq 0 ]]; then
    if [[ "$restart" -eq 0 && "$changed" -eq 1 ]]; then
        printf '%s\n' 'Fcitx5 was not reloaded (--no-restart); run fcitx5-remote -r when ready.'
    fi
    exit 0
fi

if ! command -v fcitx5-remote >/dev/null 2>&1 ||
   ! fcitx5-remote --check >/dev/null 2>&1; then
    printf '%s\n' 'Profile updated. Fcitx5 is not running; start it to use Ari IME.'
    exit 0
fi

printf '%s\n' 'Reloading Fcitx5'
fcitx5-remote -r
for _ in {1..50}; do
    if fcitx5-remote --check >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
if ! fcitx5-remote --check >/dev/null 2>&1; then
    printf '%s\n' 'Profile updated, but Fcitx5 did not become ready after reload.' >&2
    exit 1
fi

if fcitx5-remote -s inputer >/dev/null 2>&1; then
    printf 'Ari IME is active (%s)\n' "$(fcitx5-remote -n)"
else
    printf '%s\n' 'Profile updated. Select Ari IME with fcitx5-remote -s inputer.'
fi
