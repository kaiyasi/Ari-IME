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
- **Result-based tone-one input** — a pending key sequence plus Space converts
  only when libchewing actually produces a Han character. There is no
  per-key or per-word allow/deny list, so `a ` stays literal while `u` + Space
  produces `一` on the default layout; the same rule applies to out-of-order
  sequences and every supported layout.
- **English-word friendly** — a tone peels the shortest trailing syllable, so
  brand names stay intact (`aceru/6` → `acer螢`).
- **Typo-tolerant composition** — when a repeated key or an invalid leading
  key breaks a 注音 hypothesis, Ari removes it only after the remaining keys
  independently form a Chinese syllable (`HHK$G$` and `GHK$G$` → `測試`).
- **Live contextual ranking** — as soon as a complete Chinese result is
  available, libchewing applies its surrounding-word model and learned habits
  to the inline result. Ari does not open a candidate panel automatically;
  press Down to enter normal candidate picking when you want alternatives.
- **Candidate re-selection anywhere** — press ↓/←/→ to open a cursor that walks
  the whole pre-edit and re-pick any character or phrase; phrase recommendations
  start at the focused character and may extend toward following text, while
  earlier text is not pulled into the current lookup. Earlier picks stay pinned.
  Picking a candidate leaves the caret on the character right after the text it
  rewrote, so correcting something in the middle of a sentence keeps editing
  there; press End (or Esc) to go back to appending, or set **Caret after
  picking a candidate** to *Jump to the end* to always land at the tail. ←/→
  step to the neighbouring character's candidates by default and can be set to
  turn candidate pages. Candidates can be picked by number key or
  direct click/touch, and multi-page lists show their current page in the
  auxiliary line. The labeled `原始鍵 ...` candidate restores a converted
  character back to its raw keys. Literal punctuation cells use the same picker:
  ↓ exposes only the half-width, full-width, Chinese and paired variants
  associated with that key, with the current form first; for example, `[`
  can be changed to `「`, `『`, `【`, `《` and other nested brackets, without
  unrelated `!` candidates.
  If a punctuation-looking key such as `.` or `,` is also used in a 注音
  syllable, its punctuation variants are added to that Chinese character's
  candidate list as well; the complete Chinese candidate list stays first,
  punctuation follows it, and the raw-key recovery entry remains last.
  `Ctrl+Z` restores recent candidate choices until text is otherwise edited;
  `Shift+Delete` forgets the highlighted personal candidate without removing
  the same word from the built-in dictionary.
- **Committed-text reconversion** — select a short all-Chinese range that has
  already been sent to the application and press `Ctrl+Alt+R` to reopen Ari's
  native candidate editor. The original text stays available, so a phrase can
  be corrected without deleting and retyping it. This uses Fcitx5's native
  surrounding-text capability, does nothing in unsupported or sensitive fields,
  and can be cleared or rebound through the `ReconversionKey` addon setting.
- **Safe mixed-text editing** — pasted common multi-codepoint Emoji (including
  variation-selector, flag and ZWJ sequences) behaves as one unit for the caret
  and Backspace instead of being split into broken fragments.
- **Consistent phrasing** — the character shown while typing matches the top
  candidate the selection window offers ("以選字候選為準").
- **Explicit Chinese punctuation** — ordinary punctuation stays literal and
  half-width regardless of surrounding Chinese or English. Hold the configured
  `ChinesePunctuationShortcut` (default `Ctrl+Shift`) with a punctuation key to
  request its Chinese form temporarily: comma → ，, period → 。, slash → ？,
  apostrophe key (`'`) → 、, `(` → （, `{` → 『, etc. In particular, the
  default Chinese enumeration comma is **Ctrl+Shift+'**. The native Fcitx5
  setting can switch the gesture to `Alt+Shift`, `Ctrl`, `Alt`, or disable it
  when an application reserves the default shortcut.
  The dedicated `Alt+[` and `Alt+]` shortcuts produce Chinese corner quotes
  `「` and `」`; modified forms such as `Ctrl+Alt+[` remain available to the
  application.
  **FullWidthPunctuation** remains available for users who explicitly prefer
  full-width symbols without holding a modifier, including `@` → ＠,
  `%` → ％, `_` → ＿, `` ` `` → ｀ and `"` → ＂.
  Ari IME reserves no punctuation toggle shortcut by default, so common
  application shortcuts such as `Ctrl+.` remain available;
  **FullWidthPunctuationToggle** can optionally bind a modifier shortcut that
  flips the setting live and persists it.
  Other `Alt` punctuation remains available to applications instead of being
  captured.
- **Forced English mode** — `Ctrl+Space` toggles it; a transient 中/英 hint pops
  up, and the mode persists until toggled again.
- **Optional traditional Space selection** — enable the native
  `SpaceCandidateMode` setting if you prefer Space to open candidates after a
  complete Chinese conversion. It is off by default, so Ari keeps its mixed
  input Space-as-tone-one/literal-space behavior and Enter-only commit contract.
- **Visible composition status** — the auxiliary line shows current 中/英 mode,
  keyboard layout and punctuation mode while composing. This status line is off
  by default and can be re-enabled in the addon config.
- **Weighted per-user learning** — committing with Enter gives an unchanged conversion
  one weak positive learning pass. An explicitly selected character or phrase
  receives three extra passes (roughly 4:1), plus one short surrounding-context
  pass, so deliberate choices adapt faster without treating accepted defaults
  as mistakes. Explicitly selected or imported mappings, including entries
  added through Ari's dictionary API, are kept in a small Ari-owned preference
  sidecar for backup and forget bookkeeping. Live candidate order remains under
  libchewing's contextual scorer; Ari does not open a hidden candidate window on
  every completed syllable or force a static preference to the top.
  **AutoLearn** can be disabled in the addon's configuration when
  the personal dictionary should remain unchanged. The setting also controls
  libchewing's internal learner during candidate selection. Learning replays
  only train text that exactly matches the committed choice, and concurrent
  input contexts preserve each other's preference updates. Password and
  sensitive input fields never write learning data.
- **Automatic offline context** — libchewing's local phrase model uses
  surrounding words to distinguish homophones such as `我的` and `跑得快`.
  Personal weights feed the same model automatically; there is no external AI,
  network request, model download or setting to enable.
- **Portable personal dictionary** — `ari-ime-dict` can inspect the active
  libchewing version and data directory, list or export personal phrase
  mappings, validate an import without changing anything, merge an import
  safely, and create a raw-data backup. The text format stores canonical
  Unicode Bopomofo readings such as `ㄋㄧˇ`, so it is not tied to one keyboard
  layout. Package installation never imports or changes user data.
- **Punctuation-aware boundaries** — a literal symbol can be followed directly
  by Zhuyin (`(hk4g4` -> `(測試`) without trapping the following keys in an
  English token.
- **Reusable WebAssembly core** — [`wasm/`](wasm/) packages the same headless
  input state machine as `@ari-ime/wasm`. Other applications can import it and
  own keyboard events, candidate rendering, clipboard handling and persistence;
  it has no Fcitx5 or UI runtime dependency.

The WebAssembly package ships its generated `.js`, `.wasm` and dictionary data
artifacts for direct npm use. See [`wasm/README.md`](wasm/README.md) for the
Emscripten build inputs and the native/API smoke tests.

## Keys

| Key | Action |
|-----|--------|
| letters / digits | 注音 keys in the selected keyboard layout, or literal English |
| layout tone keys, space (一聲) | complete the pending syllable |
| ↓ / ← / → | open candidate re-selection over the pre-edit |
| Ctrl+Alt+R | reopen a selected short Chinese range for candidate correction (configurable) |
| ↑ | open/reinterpret the current pre-edit cell, or show a literal key's Bopomofo symbol (configurable) |
| Tab / Shift+Tab (in candidates) | move candidate highlight forward / backward |
| Home / End | jump to the beginning / end of the pre-edit |
| Ctrl+Left / Ctrl+Right | move by libchewing phrase boundaries or English words |
| Delete | delete the character right of the caret, or the focused candidate cell |
| Shift+Delete (in candidates) | forget the highlighted personal learning record |
| PageUp / PageDown | move between candidate pages |
| ← / → (in candidates) | move to the neighbouring character's candidates, or turn candidate pages (configurable) |
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
yay -S fcitx5-ari-ime-bin && ari-ime-enable --yes --make-default
# or: paru -S fcitx5-ari-ime-bin && ari-ime-enable --yes --make-default
```

After a successful package installation, the command adds Ari to the current
user's Fcitx5 profile and selects it as the default input method.

Binary archives and SHA-256 checksums are also available from
[GitHub Releases](https://github.com/kaiyasi/Ari-IME/releases).

### Build from source

[`fcitx5-ari-ime`](https://aur.archlinux.org/packages/fcitx5-ari-ime) downloads
the tagged source and builds it locally:

```sh
yay -S fcitx5-ari-ime && ari-ime-enable --yes --make-default
# or: paru -S fcitx5-ari-ime && ari-ime-enable --yes --make-default
```

Developers can instead clone this repository and use the manual source-build
instructions below.

## Install on NixOS

The repository ships a Nix flake. Its module registers Ari as a Fcitx5 addon
automatically whenever Fcitx5 is the configured input method framework, so
installation is declarative — there is no install script to run.

1. Add the flake input and import the module:

   ```nix
   # flake.nix
   inputs.ari-ime.url = "github:kaiyasi/Ari-IME";
   ```

   ```nix
   # configuration.nix
   { inputs, ... }: {
     imports = [ inputs.ari-ime.nixosModules.default ];

     i18n.inputMethod = {
       type = "fcitx5";
       fcitx5.addons = [ ]; # Ari is appended by the imported module
     };
   }
   ```

2. Rebuild and log out and back in:

   ```sh
   sudo nixos-rebuild switch --flake .#your-host
   ```

3. Add **Ari IME** (`ari-ime`) through `fcitx5-configtool`, or run
   `ari-ime-enable --yes --make-default` if you prefer the same helper used on
   other distributions.

Home Manager users who manage only their home directory can import
`homeManagerModules.default` with the same `i18n.inputMethod` options.
To wire the package manually without any module:

```nix
i18n.inputMethod.fcitx5.addons =
  [ inputs.ari-ime.packages.${pkgs.system}.default ];
```

The flake also provides `overlays.default` (adds
`pkgs.fcitx5-ari-ime`) and a development shell via `nix develop`. The package
version is read from `CMakeLists.txt`, so it always matches the source tree.

Learned personal data lives under `~/.config/ari-ime/` exactly like on other
distributions; `nixos-rebuild` never removes it. Resetting it stays available
through `ari-ime-reset-data`.

## Source-build dependencies

- fcitx5 (and `Fcitx5Core` / `Fcitx5Config` / `Fcitx5Utils` /
  `Fcitx5ModuleClipboard` development files)
- libchewing (`chewing`)
- hicolor-icon-theme (for the installed `ari-ime` icon)
- extra-cmake-modules (ECM)
- a C++20 compiler, CMake ≥ 3.16

The automated tests are written to tolerate libchewing dictionary ranking
changes where Ari IME does not own the exact candidate order. The release gate
prints the resolved libchewing version so a distribution update can be correlated
with candidate-order changes; the exact first candidate is not a cross-distro
compatibility promise.

For a source build on Arch Linux:

```sh
sudo pacman -S fcitx5 hicolor-icon-theme libchewing extra-cmake-modules cmake gcc
```

On Ubuntu / Debian:

```sh
sudo apt install \
  cmake extra-cmake-modules g++ pkg-config \
  fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev \
  fcitx5-modules-dev libchewing3-dev hicolor-icon-theme
```

`fcitx5-modules-dev` provides the clipboard module headers (`clipboard_public.h`,
`Fcitx5ModuleClipboard`) that the Ctrl+V paste path links against. Ubuntu builds
against whatever libchewing the distribution ships (for example libchewing 0.8.x
on Ubuntu 24.04); candidate ordering can differ slightly between distributions —
see [ISSUES.md](ISSUES.md). No source changes
are needed: the build uses `GNUInstallDirs`, so the module installs to the
distribution's multiarch fcitx5 directory (e.g.
`/usr/lib/x86_64-linux-gnu/fcitx5`) which fcitx5 scans automatically. Install with
`-DCMAKE_INSTALL_PREFIX=/usr` (see below) so the descriptors land under `/usr/share`.

If libchewing's system dictionary lives in a non-standard location, set
`CHEWING_PATH` to point at it; Ari IME otherwise falls back to a read-only chewing
context when its own user-dictionary directory is not writable, so composition
keeps working even without per-user learning.

### Debian / Ubuntu binary release

Each GitHub release also includes a tested `.deb` for 64-bit Debian/Ubuntu:

```sh
sudo apt install ./fcitx5-ari-ime_<version>_amd64.deb
```

Download the matching `.deb` and `.sha256` files from the
[GitHub Release](https://github.com/kaiyasi/Ari-IME/releases), then continue
with the Fcitx5 setup steps below.

For a one-command installation that downloads the latest release, verifies the
checksum, installs dependencies, adds Ari to the Fcitx5 profile, and selects it
as the default input method in the current graphical session:

```sh
curl -fsSL https://raw.githubusercontent.com/kaiyasi/Ari-IME/main/scripts/install-ubuntu.sh | bash
```

The command installs the prebuilt release package; the build itself is already
performed and tested by GitHub Actions. Run it as the normal desktop user so
`ari-ime-enable` updates that user's Fcitx5 profile. The installer also falls
back to the existing versioned release assets while older releases do not yet
have the stable `fcitx5-ari-ime_amd64.deb` alias.

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

For the shortest first-run setup, add Ari to the current Fcitx5 profile with
the installed command:

```sh
ari-ime-enable --make-default
```

This is an explicit user command, not a package hook. It changes only the first
Fcitx5 input-method group, creates a timestamped backup before changing an
existing profile, and makes Ari the default only when `--make-default` is
given. Omit that option to keep the current default input method. When run in a
graphical session, it starts Fcitx5 if needed, reloads the profile, selects Ari,
and verifies that `ari-ime` is active; in a headless shell it updates the profile
but asks you to start Fcitx5 and run the command again.

### Local user-directory install (development)

Installing into `~/.local` is useful for testing without `sudo`, but a normal
Fcitx5 desktop process does not necessarily search that directory for addons.
Use the helper below after building; it installs the module, restarts Fcitx5
with the local addon path, selects Ari IME, and verifies the loaded `.so` path:

```sh
bash scripts/install-local.sh
```

The helper uses `build-public-release` by default. For the ordinary `build`
directory, use `ARI_IME_BUILD_DIR=build bash scripts/install-local.sh`. This is
intended for the current development session. For a persistent desktop install,
use the AUR package, a GitHub release package, or install under `/usr` as shown
above so Fcitx5 finds the addon through its normal search path.

The graphical alternative is to add **Ari IME** in `fcitx5-configtool`:

1. Run `fcitx5-configtool` from a terminal or your application launcher —
   not your desktop environment's system input settings.
2. Go to the **Input Method** tab → click **+** → search **Ari** → select
   **Ari IME** → click **OK**.

Then select it once and verify the active name:

```sh
fcitx5-remote -s ari-ime
fcitx5-remote -n   # should print: ari-ime
```

### Settings

The addon's config page lists these options. Each label is deliberately short so
the page fits inside the settings window; the details below are also attached to
every option as a tooltip, which `fcitx5-config-qt` shows on hover (KDE's System
Settings module does not render tooltips yet).

| Option | Default | What it does |
|--------|---------|--------------|
| Keyboard layout | 大千 | Bopomofo key arrangement. Drives both Ari's key classification and libchewing's keyboard type. |
| Always use full-width punctuation | off | Full-width Chinese punctuation without a modifier. Off keeps ordinary punctuation literal and reserves the Chinese form for the shortcut below. |
| Chinese punctuation shortcut | Ctrl+Shift | Modifier that temporarily produces the Chinese form of a punctuation key. Pick another one if an application already uses it. `Alt+[` / `Alt+]` stay reserved for corner quotes. |
| Space opens candidates | off | Space opens the candidate window after a complete syllable. Off keeps Ari's Space-as-一聲 and literal-space behavior. Enter commits either way. |
| Left/Right in the candidate window | Move to the next character | What ←/→ do while candidates are open. **Move to the next character** walks to the neighbouring character's candidates; **Turn candidate pages** pages through the focused character's list, cycling at both ends like libchewing's own window. Whichever you don't pick stays reachable: PageUp/PageDown always page (stopping at the ends), and Esc returns to the caret where ←/→ always move. |
| Up arrow on a literal character | Merge into a Chinese character | What ↑ does to a literal English or punctuation character. **Merge into a Chinese character** folds it together with the next few when they form a complete syllable (`catsu3` → `cat` + 你). **Show its Bopomofo symbol** replaces just that one key with the symbol it stands for (`1` → `ㄅ`) as ordinary text, the way ASUS's mixed input does — the symbol is a finished character, so the next key you type follows it. |
| Caret after picking a candidate | Stay after the corrected text | Where the caret goes once a candidate is chosen. **Stay after the corrected text** keeps editing at the correction; **Jump to the end** returns to appending at the tail. |
| Reconversion shortcut | Control+Alt+R | Re-opens a short selected Chinese range for candidate correction. Clear it to reserve no shortcut. |
| Learn accepted choices locally | on | Adapts the personal dictionary to the Chinese you accept. Sensitive fields never learn regardless of this setting. |
| Show composition status | off | Shows `中 · 大千 · 半形標點`-style status in the auxiliary line while composing. |
| Show pending Bopomofo | off | Shows the symbols of the pending syllable near the cursor while typing. |
| Full-width punctuation toggle | unset | Optional shortcut that turns full-width punctuation on and off. Empty by default so no application shortcut is reserved; a modifier is required. |

## Tests

```sh
ctest --test-dir build
```

The tests isolate chewing's learned dictionary in a temp directory, so they are
deterministic and do not touch your real `~/.config` data.

Development/test safeguards around personalization:

- Automated tests set `ARI_IME_USER_DATA_DIR` and `XDG_CONFIG_HOME` to a fresh
  temp directory.
- Automated tests also set `ARI_IME_DISABLE_AUTOLEARN=1`, so no learned
  personalization is intentionally recorded during ordinary
  unit/integration/fuzz runs, and any libchewing-created artifacts stay inside
  the disposable temp directory.
- Production usage keeps auto-learning enabled by default and writes only to
  Ari IME's own user-data directory (`userdict.dat`, `chewing.dat` and
  `chewing-deleted.dat`), not libchewing's built-in dictionary resources.

For the full local verification pass:

```sh
scripts/check.sh
```

This checks version consistency across CMake/PKGBUILD/.SRCINFO, then runs the
release build, CTest, install smoke check, PKGBUILD syntax check, and the
sanitizer test profile. The version check prints the validated Ari IME version,
libchewing version, CMake version, and active C++ compiler so CI failures can be
correlated with dependency changes. Add
`ARI_IME_CHECK_PACKAGE=1` to also run an offline Arch package
`build/check/package` simulation.

Set `ARI_IME_CHECK_MODE=release`, `sanitize`, `coverage`, `fuzz`, or `package`
to run just one part of the check. GitHub Actions uses the release, sanitizer,
bounded-fuzz, and package modes as separate jobs in an Arch Linux container on
pushes and pull requests.

### Verifying on another distribution

`scripts/check.sh` assumes an Arch-like host. To build and test this source tree
on a Debian, Ubuntu, Arch or Fedora machine — useful because the fcitx5 headers
it compiles against and the libchewing that ranks its candidates are whatever
the distribution ships:

```sh
scripts/build-from-source.sh --deps
```

It prints a banner with the distribution, compiler, CMake, libchewing and fcitx5
versions before building, so a failure is attributable to a specific dependency
set; then it configures with `-DBUILD_TESTING=ON`, builds, stages an install
into a throwaway prefix, and runs CTest. `--deps` installs the build
dependencies for the detected distribution (needs sudo) and can be dropped on
later runs.

To install what you just built, the way the distribution expects:

```sh
scripts/build-from-source.sh --install
```

That builds the native package, installs it through `apt` or `pacman` (so it
stays uninstallable and does not fight the package manager), then restarts
Fcitx5 and reports which module the running daemon actually loaded. The restart
matters: Fcitx5 `dlopen()`s the addon once at startup, and `fcitx5-remote -r`
rereads configuration without swapping the library — so after an upgrade the
daemon keeps running the previous build out of a file that has already been
replaced. The script tells you which state you are in:

```
    Fcitx5 pid 146938
    loaded: /usr/lib/fcitx5/ari-ime.so
```

A path ending in `(deleted)` means the daemon is still on the old build and
needs `fcitx5 -r -d`. Pass `--no-restart` to leave a running Fcitx5 alone.

If Ari IME is not in your input method group yet, add it once with
`ari-ime-enable --make-default`.

Add `--package` to build the distribution's own package instead — a `.deb` via
`dpkg-buildpackage` on Debian and Ubuntu (whose `debian/rules` configures with
`-DBUILD_TESTING=ON`, so debhelper runs CTest as part of the build), or a
`.pkg.tar.zst` via `makepkg` on Arch. The Arch path builds from a copy of the
tree rather than using the shipped `PKGBUILD` directly, because that one
downloads the released tarball from GitHub and uses `$startdir/src` — this
project's actual source directory — as its build directory.

Copy the tree to the target machine first; the script builds what is on disk,
not a release tarball:

```sh
rsync -a --exclude build --exclude '.git' ./ user@host:ari-ime/
```

For memory/undefined-behavior checks:

```sh
cmake -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DARI_IME_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Leak detection is disabled in this profile so the tests still run in ptrace-based
sandboxes. To include LeakSanitizer on a normal local/CI runner, add
`-DARI_IME_SANITIZER_DETECT_LEAKS=ON`.

For a local gcov coverage report:

```sh
ARI_IME_CHECK_MODE=coverage scripts/check.sh
```

This builds the tests with `-DARI_IME_ENABLE_COVERAGE=ON`, runs CTest, and writes
`.gcov` reports for the main `src/` state-machine files to
`build-coverage/gcov/`.

For bounded state-machine fuzzing with libFuzzer:

```sh
cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DARI_IME_ENABLE_FUZZING=ON
cmake --build build-fuzz --target fuzz_buffer
./build-fuzz/fuzz_buffer -runs=1000
```

The fuzz target is opt-in and is not part of the normal release/package build.
It feeds mixed key, paste, candidate-selection, layout-switch, and punctuation
events into `Buffer` while checking UTF-8 and public caret/candidate invariants.
Use `ARI_IME_CHECK_MODE=fuzz scripts/check.sh` for the same bounded smoke run
that CI uses. It loads the seed corpus in `test/corpus/fuzz_buffer` when present;
the check script copies those seeds into a temporary corpus first so local fuzz
runs do not dirty the tracked seed directory. Printable ASCII bytes in those
seeds are interpreted as direct key presses. Set
`ARI_IME_FUZZ_RUNS` to adjust the run count, or `ARI_IME_FUZZ_CORPUS_DIR` to
point at another corpus directory. Set `ARI_IME_FUZZ_ARTIFACT_DIR` to make
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
 [docs/release-2.3.6.md](docs/release-2.3.6.md).

## Resetting learned data

Ari IME stores its learned per-user data in its own directory:

- `${ARI_IME_USER_DATA_DIR}`, when `ARI_IME_USER_DATA_DIR` is set
- otherwise `${XDG_CONFIG_HOME:-$HOME/.config}/ari-ime/`

That directory holds `userdict.dat`, Ari's explicit `preferences.tsv`, plus
libchewing's learned files (`chewing.dat`, `chewing-deleted.dat`). Ari pins
`CHEWING_USER_PATH` to this directory so learning does not leak into the shared
`$XDG_DATA_HOME/chewing` used by other libchewing input methods. The sidecar
contains only phrases explicitly selected, imported, or added through Ari's
dictionary API for portable bookkeeping; the other files hold the learned
phrase/homophone frequencies that drive live ranking.
They are safe to reset without affecting libchewing's built-in/base dictionary.

Older Ari builds (before this pinning) may have left learned data in
`~/.local/share/chewing`; pass `--include-shared` to the reset script below to
clear that shared location too (it may be shared with other chewing IMEs).
Learning collected before the weighted scheme does not distinguish unchanged
output from explicit selections. It remains usable, but a one-time reset is a
useful diagnostic if old candidate ordering still feels inconsistent.

To reset learned data safely for development or local troubleshooting:

```sh
ari-ime-reset-data
```

The same script is available as `scripts/reset-user-data.sh` from a source
checkout.

## Moving personal dictionary data

The package-installed `ari-ime-dict` command provides a no-UI way to inspect and
move Ari's personal phrase mappings:

```sh
ari-ime-dict info
ari-ime-dict candidates su3
ari-ime-dict export "$HOME/ari-ime-dictionary.tsv"
ari-ime-dict import --dry-run "$HOME/ari-ime-dictionary.tsv"
ari-ime-dict import "$HOME/ari-ime-dictionary.tsv"
ari-ime-dict backup
```

Imports are merges, are idempotent, and create a timestamped backup before any
existing libchewing data is changed. The portable text file contains a phrase
and libchewing's canonical Unicode Bopomofo reading separated by a tab; use a
reading such as `ㄋㄧˇ`, not layout-specific keys such as `su3`. The portable
format transfers personal phrase mappings, while exact frequency state can
still vary with the installed libchewing version; `backup` preserves the raw
files for same-engine recovery. `candidates KEYS` prints the current engine
result and candidate page for a raw 大千 key sequence such as `su3` or `hk4g4`,
which makes candidate-order reports reproducible without changing the active
Fcitx5 session.

The script backs up the current `userdict.dat` to a timestamped `.bak.*` file
and removes the active learned dictionary so Ari IME starts relearning from a
clean state. Use `--yes` for non-interactive use, or `--no-backup` if you
explicitly want to discard the existing learned file.

For test/dev isolation, point Ari IME at a disposable user-data directory:

```sh
export ARI_IME_USER_DATA_DIR=/tmp/ari-ime-dev-userdata
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
