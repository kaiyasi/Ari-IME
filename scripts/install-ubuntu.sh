#!/usr/bin/env bash
set -euo pipefail

# Install the latest prebuilt Ari IME Debian package and make it active in the
# current user's graphical Fcitx5 session. This script is intentionally an
# explicit user action; the Debian package itself does not change the user's
# input-method profile during package installation.

repository="${ARI_IME_REPOSITORY:-kaiyasi/Ari-IME}"
asset_name="fcitx5-ari-ime_amd64.deb"
download_root="$(mktemp -d "${TMPDIR:-/tmp}/ari-ime-install.XXXXXX")"
deb_path="$download_root/$asset_name"
checksum_url=""

cleanup() {
    rm -rf -- "$download_root"
}
trap cleanup EXIT

die() {
    printf 'Ari IME installation failed: %s\n' "$*" >&2
    exit 1
}

if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    die 'run this command as your normal desktop user, not as root'
fi
command -v curl >/dev/null 2>&1 || die 'curl is required'
command -v apt-get >/dev/null 2>&1 || die 'this installer requires Debian or Ubuntu'
command -v sudo >/dev/null 2>&1 || die 'sudo is required'

sudo -v

release_download="https://github.com/$repository/releases/latest/download"
fixed_url="$release_download/$asset_name"

# New releases publish a stable asset name. Fall back to the versioned asset
# through the GitHub API so this installer also works with older releases.
if curl --fail --location --silent --show-error --retry 3 \
    "$fixed_url" --output "$deb_path"; then
    checksum_url="$fixed_url.sha256"
else
    rm -f -- "$deb_path"
    release_json="$(curl --fail --location --silent --show-error --retry 3 \
        -H 'Accept: application/vnd.github+json' \
        "https://api.github.com/repos/$repository/releases/latest")" \
        || die 'could not query the latest GitHub release'
    versioned_url="$(printf '%s\n' "$release_json" | sed -nE \
        's/.*"browser_download_url": "([^\"]*fcitx5-ari-ime_[^\"]*_amd64\.deb)".*/\1/p' |
        head -n 1)"
    [[ -n "$versioned_url" ]] || die 'the latest GitHub release has no amd64 Debian package'
    curl --fail --location --silent --show-error --retry 3 \
        "$versioned_url" --output "$deb_path" \
        || die 'could not download the latest Debian package'
    checksum_url="${versioned_url%.deb}.sha256"
fi

package_name="$(dpkg-deb --field "$deb_path" Package 2>/dev/null || true)"
[[ "$package_name" == fcitx5-ari-ime ]] || die 'the downloaded file is not an Ari IME Debian package'
package_arch="$(dpkg-deb --field "$deb_path" Architecture 2>/dev/null || true)"
system_arch="$(dpkg --print-architecture 2>/dev/null || true)"
[[ "$package_arch" == "$system_arch" || "$package_arch" == all ]] ||
    die "the package is for $package_arch, but this system is $system_arch"

# Releases publish a checksum beside every package. Verify it when available
# before handing the package to apt.
checksum="$(curl --fail --location --silent --show-error --retry 3 \
    "$checksum_url" | awk 'NR == 1 { print $1; exit }' || true)"
if [[ "$checksum" =~ ^[[:xdigit:]]{64}$ ]]; then
    actual_checksum="$(sha256sum "$deb_path" | awk '{ print $1 }')"
    [[ "$checksum" == "$actual_checksum" ]] || die 'the Debian package checksum does not match'
else
    die 'the latest GitHub release has no usable Debian package checksum'
fi

sudo apt-get update
(
    cd -- "$download_root"
    sudo apt-get install --yes "./$asset_name"
)

command -v ari-ime-enable >/dev/null 2>&1 ||
    die 'the package installed without ari-ime-enable'

# This also reloads an existing daemon, starts it in a graphical session when
# needed, selects Ari, and verifies that inputer is the active IME.
ari-ime-enable --yes --make-default
printf '%s\n' 'Ari IME was downloaded, installed, enabled, and selected as default.'
