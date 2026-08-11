# Ari IME

A Fcitx5 input method for Traditional Chinese that lets you type **Bopomofo (注音)
and English together without switching modes**. Every key first shows as itself;
keys only become Chinese once they form a complete, toned 注音 syllable
(e.g. `su` stays `su` until a tone arrives — `su3` → 你). The whole pre-edit may
freely mix English and Chinese in any order (e.g. `acer螢幕`) and is only sent to
the application when you press **Enter**.

Built on [libchewing](https://github.com/chewing/libchewing) for conversion,
phrasing and per-user learning.

## Features

- **Mixed input, no mode switching** — type `acer螢幕` in one go.
- **Out-of-order tolerant** — `su3` and `s3u` both produce 你.
- **Result-based tone-one input** — a single key plus Space converts only when
  libchewing actually produces a Han character. There is no per-key allow/deny
  list, so `a ` stays literal while `u` + Space produces `一` on the default
  layout, with the same rule applied to every supported layout.
- **English-word friendly** — a tone peels the shortest trailing syllable, so
  brand names stay intact (`aceru/6` → `acer螢`).
- **Candidate re-selection anywhere** — press ↓/←/→ to open a cursor that walks
  the whole pre-edit and re-pick any character or phrase; earlier picks stay
  pinned. Candidates can be picked by number key or direct click/touch, and
  multi-page lists show their current page in the auxiliary line. The labeled
  `原始鍵 ...` candidate restores a converted character back to its raw keys.
  `Ctrl+Z` restores recent candidate choices until text is otherwise edited;
  `Shift+Delete` forgets the highlighted personal candidate without removing
  the same word from the built-in dictionary.
- **Consistent phrasing** — the character shown while typing matches the top
  candidate the selection window offers ("以選字候選為準").
- **Explicit Chinese punctuation** — ordinary punctuation stays literal and
  half-width regardless of surrounding Chinese or English. Hold `Ctrl+Shift`
  with a punctuation key to request its Chinese form temporarily: comma → ，,
  period → 。, slash → ？, apostrophe → 、, `(` → （, `{` → 『, etc.
  **FullWidthPunctuation** remains available for users who explicitly prefer
  full-width symbols without holding a modifier, including `@` → ＠,
  `%` → ％, `_` → ＿, `` ` `` → ｀ and `"` → ＂.
  Ari IME reserves no punctuation toggle shortcut by default, so common
  application shortcuts such as `Ctrl+.` remain available;
  **FullWidthPunctuationToggle** can optionally bind a modifier shortcut that
  flips the setting live and persists it.
  `Alt` punctuation remains available to applications instead of being captured.
- **Forced English mode** — `Ctrl+Space` toggles it; a transient 中/英 hint pops
  up, and the mode persists until toggled again.
- **Visible composition status** — the auxiliary line shows current 中/英 mode,
  keyboard layout and punctuation mode while composing. This status line is off
  by default and can be re-enabled in the addon config.
- **Weighted per-user learning** — pressing Enter gives an unchanged conversion
  one weak positive learning pass. An explicitly selected character or phrase
  receives three extra passes (roughly 4:1), plus one short surrounding-context
  pass, so deliberate choices adapt faster without treating accepted defaults
  as mistakes.
  Password and sensitive input fields never write learning data.
- **Automatic offline context** — libchewing's local phrase model uses
  surrounding words to distinguish homophones such as `我的` and `跑得快`.
  Personal weights feed the same model automatically; there is no external AI,
  network request, model download or setting to enable.
- **Punctuation-aware boundaries** — a literal symbol can be followed directly
  by Zhuyin (`(hk4g4` -> `(測試`) without trapping the following keys in an
  English token.

## Keys

| Key | Action |
|-----|--------|
| letters / digits | 注音 keys in the selected keyboard layout, or literal English |
| layout tone keys, space (一聲) | complete the pending syllable |
| ↓ / ← / → | open candidate re-selection over the pre-edit |
| ↑ | open/reinterpret the current pre-edit cell |
| Tab / Shift+Tab (in candidates) | move candidate highlight forward / backward |
| Home / End | jump to the beginning / end of the pre-edit |
| Ctrl+Left / Ctrl+Right | move by libchewing phrase boundaries or English words |
| Delete | delete the character right of the caret, or the focused candidate cell |
| Shift+Delete (in candidates) | forget the highlighted personal learning record |
| PageUp / PageDown | move between candidate pages |
| number `1`–`9` | pick a candidate |
| Backspace (in selection) | delete the focused character and leave selection |
| Esc | clear pre-edit, or close selection/candidates first |
| Enter | commit the whole pre-edit to the application |
| Ctrl+V / Shift+Insert | paste clipboard text at the current pre-edit caret, with control/newline-like separators folded into visible spaces and zero-width artifacts removed |
| Ctrl+Z | restore the most recent candidate choice while it is still the latest edit |
| Ctrl+Space | toggle forced English mode |

Numeric-keypad navigation keys are treated like their main-keyboard equivalents
when NumLock is off. Numeric-keypad digits remain literal digits when NumLock is
on, so they do not accidentally become tone keys. `Shift+KP_Insert` also pastes.

## Keyboard layout

Currently supported layouts:

- **大千** (`KB_DEFAULT`) — `su3` → 你, `su3cl3` → 你好
- **倚天** (`KB_ET`) — `ne3` → 你, `ne3hz3` → 你好
- **許氏** (`KB_HSU`) — `nef` → 你, `nefhwf` → 你好
- **IBM** (`KB_IBM`) — `7a,` → 你, `7a,-;,` → 你好
- **精業** (`KB_GIN_YIEH`) — `d-a` → 你, `d-avla` → 你好
- **Dvorak** (`KB_DVORAK`) — `og3` → 你, `og3jn3` → 你好
- **Carpalx** (`KB_CARPALX`) — `su3` → 你, `su3cl3` → 你好
- **Colemak-DH ANSI** (`KB_COLEMAK_DH_ANSI`) — `rl3` → 你, `rl3di3` → 你好
- **Colemak-DH Ortholinear** (`KB_COLEMAK_DH_ORTH`) — `rl3` → 你, `rl3ci3` → 你好
- **Workman** (`KB_WORKMAN`) — `sf3` → 你, `sf3mo3` → 你好
- **Colemak** (`KB_COLEMAK`) — `rl3` → 你, `rl3ci3` → 你好

The addon's config exposes the keyboard layout setting with these display names,
and the key classification plus libchewing keyboard type share one layout layer,
so other layouts can be added without changing the input state machine.
Changing the layout clears the current uncommitted pre-edit and shows a transient
keyboard-layout hint.
Pinyin keyboard modes are intentionally not exposed here because this engine's
state machine is built around one-key-per-Bopomofo-symbol layouts.

## Install on Arch Linux

Choose one of the two AUR packages below. They conflict with each other because
both install the same input-method module.

### Prebuilt package (recommended)

[`fcitx5-ari-ime-bin`](https://aur.archlinux.org/packages/fcitx5-ari-ime-bin)
downloads the tested GitHub Release binary. It does not install a compiler,
CMake or other build tools:

```sh
yay -S fcitx5-ari-ime-bin
# or: paru -S fcitx5-ari-ime-bin
```

Binary archives and SHA-256 checksums are also available from
[GitHub Releases](https://github.com/kaiyasi/Ari-IME/releases).

### Build from source

[`fcitx5-ari-ime`](https://aur.archlinux.org/packages/fcitx5-ari-ime) downloads
the tagged source and builds it locally:

```sh
yay -S fcitx5-ari-ime
# or: paru -S fcitx5-ari-ime
```

Developers can instead clone this repository and use the manual source-build
instructions below.

## Source-build dependencies

- fcitx5 (and `Fcitx5Core` / `Fcitx5Config` / `Fcitx5Utils` /
  `Fcitx5ModuleClipboard` development files)
- libchewing (`chewing`)
- hicolor-icon-theme (for the installed `inputer` icon)
- extra-cmake-modules (ECM)
- a C++20 compiler, CMake ≥ 3.16

The automated tests are written to tolerate libchewing dictionary ranking
changes where Ari IME does not own the exact candidate order. Current local and
CI verification is exercised with libchewing 0.12.0 on Arch Linux.

For a source build on Arch Linux:

```sh
sudo pacman -S fcitx5 hicolor-icon-theme libchewing extra-cmake-modules cmake gcc
```

On Ubuntu / Debian:

```sh
sudo apt install \
  cmake extra-cmake-modules g++ pkg-config \
  fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev \
  fcitx5-modules libchewing3-dev hicolor-icon-theme
```

`fcitx5-modules` provides the clipboard module headers (`clipboard_public.h`,
`Fcitx5ModuleClipboard`) that the Ctrl+V paste path links against. Ubuntu builds
against whatever libchewing the distribution ships (for example libchewing 0.8.x
on Ubuntu 24.04), which may differ from the 0.12.0 used in CI; candidate ordering
can differ slightly as a result — see [ISSUES.md](ISSUES.md). No source changes
are needed: the build uses `GNUInstallDirs`, so the module installs to the
distribution's multiarch fcitx5 directory (e.g.
`/usr/lib/x86_64-linux-gnu/fcitx5`) which fcitx5 scans automatically. Install with
`-DCMAKE_INSTALL_PREFIX=/usr` (see below) so the descriptors land under `/usr/share`.

If libchewing's system dictionary lives in a non-standard location, set
`CHEWING_PATH` to point at it; Ari IME otherwise falls back to a read-only chewing
context when its own user-dictionary directory is not writable, so composition
keeps working even without per-user learning.

## Build & install

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

On Ubuntu/Debian (and other distros where fcitx5 only scans `/usr`), add
`-DCMAKE_INSTALL_PREFIX=/usr` to the configure step so the addon and descriptors
install where fcitx5 looks for them. On Debian/Ubuntu you can also build a `.deb`
straight from the tree with `dpkg-buildpackage -us -uc -b` (see the `debian/`
directory).

Then restart fcitx5:

```sh
fcitx5 -r
```

Add **Ari IME** in fcitx5-configtool:

1. Run `fcitx5-configtool` from a terminal or your application launcher —
   not your desktop environment's system input settings.
2. Go to the **Input Method** tab → click **+** → search **Ari** → select
   **Ari IME** → click **OK**.

Per-addon options (keyboard layout, full-width punctuation) appear under the
addon's config page.

## Tests

```sh
ctest --test-dir build
```

The tests isolate chewing's learned dictionary in a temp directory, so they are
deterministic and do not touch your real `~/.config` data.

Development/test safeguards around personalization:

- Automated tests set `INPUTER_USER_DATA_DIR` and `XDG_CONFIG_HOME` to a fresh
  temp directory.
- Automated tests also set `INPUTER_DISABLE_AUTOLEARN=1`, so no learned
  personalization is intentionally recorded during ordinary
  unit/integration/fuzz runs, and any libchewing-created artifacts stay inside
  the disposable temp directory.
- Production usage keeps auto-learning enabled by default and writes only to
  Ari IME's own `userdict.dat`, not libchewing's built-in dictionary resources.

For the full local verification pass:

```sh
scripts/check.sh
```

This checks version consistency across CMake/PKGBUILD/.SRCINFO, then runs the
release build, CTest, install smoke check, PKGBUILD syntax check, and the
sanitizer test profile. The version check prints the validated Ari IME version,
libchewing version, CMake version, and active C++ compiler so CI failures can be
correlated with dependency changes. Add
`INPUTER_CHECK_PACKAGE=1` to also run an offline Arch package
`build/check/package` simulation.

Set `INPUTER_CHECK_MODE=release`, `sanitize`, `coverage`, `fuzz`, or `package`
to run just one part of the check. GitHub Actions uses the release, sanitizer,
bounded-fuzz, and package modes as separate jobs in an Arch Linux container on
pushes and pull requests.

For memory/undefined-behavior checks:

```sh
cmake -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DINPUTER_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Leak detection is disabled in this profile so the tests still run in ptrace-based
sandboxes. To include LeakSanitizer on a normal local/CI runner, add
`-DINPUTER_SANITIZER_DETECT_LEAKS=ON`.

For a local gcov coverage report:

```sh
INPUTER_CHECK_MODE=coverage scripts/check.sh
```

This builds the tests with `-DINPUTER_ENABLE_COVERAGE=ON`, runs CTest, and writes
`.gcov` reports for the main `src/` state-machine files to
`build-coverage/gcov/`.

For bounded state-machine fuzzing with libFuzzer:

```sh
cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DINPUTER_ENABLE_FUZZING=ON
cmake --build build-fuzz --target fuzz_buffer
./build-fuzz/fuzz_buffer -runs=1000
```

The fuzz target is opt-in and is not part of the normal release/package build.
It feeds mixed key, paste, candidate-selection, layout-switch, and punctuation
events into `Buffer` while checking UTF-8 and public caret/candidate invariants.
Use `INPUTER_CHECK_MODE=fuzz scripts/check.sh` for the same bounded smoke run
that CI uses. It loads the seed corpus in `test/corpus/fuzz_buffer` when present;
the check script copies those seeds into a temporary corpus first so local fuzz
runs do not dirty the tracked seed directory. Printable ASCII bytes in those
seeds are interpreted as direct key presses. Set
`INPUTER_FUZZ_RUNS` to adjust the run count, or `INPUTER_FUZZ_CORPUS_DIR` to
point at another corpus directory. Set `INPUTER_FUZZ_ARTIFACT_DIR` to make
libFuzzer write crash reproducers to a dedicated directory for CI artifact
upload.

GitHub also runs a separate scheduled/manual **Nightly Fuzz** workflow with a
larger default run count. Trigger it manually from Actions and set the `runs`
input when you want a longer one-off fuzz pass without slowing down normal
push/PR checks.

Real application behavior still needs manual validation because preedit,
candidate windows, clipboard, and theme rendering depend on the desktop session.
Use [docs/manual-qa.md](docs/manual-qa.md) before releases.

Release-specific notes are tracked in [CHANGELOG.md](CHANGELOG.md) and
[docs/release-2.0.0.md](docs/release-2.0.0.md).

## Resetting learned data

Ari IME stores its learned per-user data in its own directory:

- `${INPUTER_USER_DATA_DIR}`, when `INPUTER_USER_DATA_DIR` is set
- otherwise `${XDG_CONFIG_HOME:-$HOME/.config}/inputer/`

That directory holds `userdict.dat` plus libchewing's learned files
(`chewing.dat`, `chewing-deleted.dat`). Ari pins `CHEWING_USER_PATH` to this
directory so learning does not leak into the shared `$XDG_DATA_HOME/chewing`
used by other libchewing input methods. These files hold learned
phrase/homophone preferences and are safe to reset without affecting
libchewing's built-in/base dictionary.

Older Ari builds (before this pinning) may have left learned data in
`~/.local/share/chewing`; pass `--include-shared` to the reset script below to
clear that shared location too (it may be shared with other chewing IMEs).
Learning collected before the weighted scheme does not distinguish unchanged
output from explicit selections. It remains usable, but a one-time reset is a
useful diagnostic if old candidate ordering still feels inconsistent.

To reset learned data safely for development or local troubleshooting:

```sh
scripts/reset-user-data.sh
```

The script backs up the current `userdict.dat` to a timestamped `.bak.*` file
and removes the active learned dictionary so Ari IME starts relearning from a
clean state. Use `--yes` for non-interactive use, or `--no-backup` if you
explicitly want to discard the existing learned file.

For test/dev isolation, point Ari IME at a disposable user-data directory:

```sh
export INPUTER_USER_DATA_DIR=/tmp/inputer-dev-userdata
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
