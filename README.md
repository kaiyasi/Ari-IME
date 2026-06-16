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
  pinned.
- **Consistent phrasing** — the character shown while typing matches the top
  candidate the selection window offers ("以選字候選為準").
- **Full-width punctuation (optional)** — toggle in config; `<` → ，, `>` → 。,
  `?` → ？, etc. Off by default (half-width).
- **Forced English mode** — `Ctrl+Space` toggles it; a transient 中/英 hint pops up.
- **Per-user learning** — chewing records your homophone/phrase choices.

## Keys

| Key | Action |
|-----|--------|
| letters / digits | 注音 keys (大千 layout) or literal English |
| tone `3 4 6 7`, space (一聲) | complete the pending syllable |
| ↓ / ← / → | open candidate re-selection over the pre-edit |
| ↑ | reinterpret an English run as 注音 / revert a 注音 cell to raw keys |
| number `1`–`9` | pick a candidate |
| Backspace (in selection) | delete the focused character and leave selection |
| Enter | commit the whole pre-edit to the application |
| Ctrl+Space | toggle forced English mode |

## Keyboard layout

Currently **大千 (KB_DEFAULT) only** — the syllable classification is layout-specific.
Other layouts (倚天 / 許氏 / …) are a planned addition.

## Dependencies

- fcitx5 (and `Fcitx5Core` / `Fcitx5Utils` development files)
- libchewing (`chewing`)
- extra-cmake-modules (ECM)
- a C++20 compiler, CMake ≥ 3.16

On Arch Linux:

```sh
sudo pacman -S fcitx5 libchewing extra-cmake-modules cmake gcc
```

## Build & install

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

Then restart fcitx5 and add **知字 (Ari IME)** in `fcitx5-configtool` (or your IME
configuration). Per-addon options (e.g. full-width punctuation) appear under the
addon's config page.

## Tests

```sh
ctest --test-dir build
```

The tests isolate chewing's learned dictionary in a temp directory, so they are
deterministic and do not touch your real `~/.config` data.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
