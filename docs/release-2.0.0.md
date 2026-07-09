# Ari IME 2.0.0

## Release Summary

Ari IME 2.0.0 focuses on robustness, distribution, and correct handling of
libchewing's learned dictionary. The engine now degrades gracefully when
libchewing fails to initialize, keeps per-user learning inside Ari's own data
directory, and ships first-class Ubuntu/Debian packaging alongside the existing
Arch package.

This is a major release because the learned-dictionary location changed: earlier
versions let libchewing store learning in the shared `~/.local/share/chewing`,
while 2.0.0 pins it to Ari's own directory. Existing learning at the old shared
location is no longer read — see Upgrade Notes.

## Short Description

Ari IME is a Fcitx5 input method for Traditional Chinese that supports mixed
Bopomofo and English composition, context-aware candidates, editable in-place
preedit text, and per-user learning on top of libchewing.

## Highlights

- Graceful degrade to English passthrough (with a one-time hint) when the
  libchewing engine cannot initialize, instead of silently swallowing keys
- Learned dictionary pinned to Ari's own directory via `CHEWING_USER_PATH`, so
  it no longer pollutes the shared `$XDG_DATA_HOME/chewing`
- Optional, configurable `FullWidthPunctuationToggle` shortcut (unset by
  default, so no application shortcut is reserved)
- Ubuntu/Debian support: apt dependencies, a `debian/` packaging directory, and
  a native Ubuntu build/test/package CI workflow
- More reliable tests: `TempConfigHome` now fully sandboxes libchewing's learned
  data, so runs are deterministic regardless of real day-to-day typing

## Included In 2.0.0

- Engine-failure degrade path with read-only system-dictionary retry
- `CHEWING_USER_PATH` pinning and matching test-isolation fix
- `scripts/reset-user-data.sh` clears `chewing.dat`/`chewing-deleted.dat` and
  gained `--include-shared`
- Ubuntu/Debian packaging, docs, and CI; `debian/changelog` added to the version
  consistency check
- `ISSUES.md` known-limitations document
- Release metadata and packaging synchronized to `v2.0.0`

## Upgrade Notes

- **Learned data location changed.** Learning now lands in `~/.config/inputer/`
  rather than the shared `~/.local/share/chewing`. Personalization learned by
  earlier versions stays at the old location and is no longer used; it can be
  cleared with `scripts/reset-user-data.sh --include-shared`. New learning starts
  fresh in Ari's own directory.
- If you previously installed a local override under `~/.local/lib/fcitx5`,
  replace it with the updated build (or remove it so `/usr/lib/fcitx5` is used).
- Restart Fcitx5 after installing the updated package:

  ```sh
  fcitx5 -r
  ```

## Installation

### Arch Linux (AUR)

```sh
yay -S fcitx5-ari-ime      # or: paru -S fcitx5-ari-ime
fcitx5 -r
```

Or install a downloaded package asset:

```sh
sudo pacman -U fcitx5-ari-ime-2.0.0-1-x86_64.pkg.tar.zst
fcitx5 -r
```

### Ubuntu / Debian

```sh
sudo apt install ./fcitx5-ari-ime_2.0.0_amd64.deb
fcitx5 -r
```

Then add Ari IME in fcitx5-configtool:

1. Run `fcitx5-configtool` from a terminal or your application launcher.
2. Go to the **Input Method** tab.
3. Click **+** (Add Input Method).
4. Search for **Ari** and select **Ari IME**.
5. Click **OK** / **Apply**.

> **Note:** Make sure you opened `fcitx5-configtool`, not your desktop
> environment's system input settings or ibus/hime preferences — those panels
> will not list Ari IME.

### From Source

```sh
git clone https://github.com/kaiyasi/Ari-IME.git
cd Ari-IME
git checkout v2.0.0
cmake -B build -DCMAKE_BUILD_TYPE=Release   # add -DCMAKE_INSTALL_PREFIX=/usr on Debian/Ubuntu
cmake --build build
sudo cmake --install build
fcitx5 -r
```

## Verification

Recommended local verification before publishing or packaging:

```sh
scripts/check.sh
```

## Assets / Copy

Short release copy:

> Ari IME 2.0.0 hardens engine startup, keeps per-user learning self-contained,
> adds a configurable full-width punctuation shortcut, and ships Ubuntu/Debian
> packaging alongside Arch.

One-line store / release description:

> A Fcitx5 Bopomofo input method with mixed Chinese/English composition,
> context-aware candidates, and robust, self-contained per-user learning.
