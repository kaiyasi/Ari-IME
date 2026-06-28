# 知字 · Ari IME

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
- **English-word friendly** — a tone peels the shortest trailing syllable, so
  brand names stay intact (`aceru/6` → `acer螢`).
- **Candidate re-selection anywhere** — press ↓/←/→ to open a cursor that walks
  the whole pre-edit and re-pick any character or phrase; earlier picks stay
  pinned. Candidates can be picked by number key or direct click/touch, and
  multi-page lists show their current page in the auxiliary line. The labeled
  `原始鍵 ...` candidate restores a converted character back to its raw keys.
- **Consistent phrasing** — the character shown while typing matches the top
  candidate the selection window offers ("以選字候選為準").
- **Full-width punctuation (optional)** — toggle in config; `<` → ，, `>` → 。,
  `?` → ？, `\` → 、, `^` → ……, `(` → （, `{` → 『, etc. It applies
  in mixed Chinese/English pre-edit text, including common symbols such as
  `@` → ＠, `%` → ％, `_` → ＿, `` ` `` → ｀ and `"` → ＂. Off by default
  (half-width), with a transient hint when the setting changes. Ari IME
  deliberately does not reserve a fixed punctuation toggle shortcut, so common
  application shortcuts such as `Ctrl+.` remain available.
- **Forced English mode** — `Ctrl+Space` toggles it; a transient 中/英 hint pops
  up, and the mode persists until toggled again.
- **Visible composition status** — the auxiliary line shows current 中/英 mode,
  keyboard layout and punctuation mode while composing.
- **Per-user learning** — chewing records your homophone/phrase choices.

## Keys

| Key | Action |
|-----|--------|
| letters / digits | 注音 keys in the selected keyboard layout, or literal English |
| layout tone keys, space (一聲) | complete the pending syllable |
| ↓ / ← / → | open candidate re-selection over the pre-edit |
| ↑ | open/reinterpret the current pre-edit cell |
| Tab / Shift+Tab (in candidates) | move candidate highlight forward / backward |
| Home / End | jump to the beginning / end of the pre-edit |
| Delete | delete the character right of the caret, or the focused candidate cell |
| PageUp / PageDown | move between candidate pages |
| number `1`–`9` | pick a candidate |
| Backspace (in selection) | delete the focused character and leave selection |
| Esc | clear pre-edit, or close selection/candidates first |
| Enter | commit the whole pre-edit to the application |
| Ctrl+V / Shift+Insert | paste clipboard text at the current pre-edit caret, with control/newline-like separators folded into visible spaces and zero-width artifacts removed |
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

## Dependencies

- fcitx5 (and `Fcitx5Core` / `Fcitx5Config` / `Fcitx5Utils` /
  `Fcitx5ModuleClipboard` development files)
- libchewing (`chewing`)
- hicolor-icon-theme (for the installed `inputer` icon)
- extra-cmake-modules (ECM)
- a C++20 compiler, CMake ≥ 3.16

The automated tests are written to tolerate libchewing dictionary ranking
changes where Ari IME does not own the exact candidate order. Current local and
CI verification is exercised with libchewing 0.12.0 on Arch Linux.

On Arch Linux:

```sh
sudo pacman -S fcitx5 hicolor-icon-theme libchewing extra-cmake-modules cmake gcc
```

## Build & install

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

Then restart fcitx5 and add **知字 (Ari IME)** in `fcitx5-configtool` (or your IME
configuration). Per-addon options (keyboard layout, full-width punctuation)
appear under the addon's config page.

## Tests

```sh
ctest --test-dir build
```

The tests isolate chewing's learned dictionary in a temp directory, so they are
deterministic and do not touch your real `~/.config` data.

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
point at another corpus directory.

GitHub also runs a separate scheduled/manual **Nightly Fuzz** workflow with a
larger default run count. Trigger it manually from Actions and set the `runs`
input when you want a longer one-off fuzz pass without slowing down normal
push/PR checks.

Real application behavior still needs manual validation because preedit,
candidate windows, clipboard, and theme rendering depend on the desktop session.
Use [docs/manual-qa.md](docs/manual-qa.md) before releases.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
