# Ari IME 1.0.0

## Release Summary

Ari IME 1.0.0 is the first stable release of the project. It delivers mixed
Bopomofo and English composition without mode switching, context-aware
candidate handling, editable in-place preedit text, and per-user learning on
top of libchewing.

## Short Description

Ari IME is a Fcitx5 input method for Traditional Chinese that lets you type
Bopomofo and English together in a single preedit flow, with phrase candidates,
mid-string editing, and per-user learning.

## Highlights

- Mixed Traditional Chinese and English composition in one preedit buffer
- Context-aware candidate ranking and punctuation handling
- Candidate re-selection anywhere in the active preedit
- Mid-string insertion, caret editing, and raw-key reversion
- Resettable per-user learned dictionary with test-safe isolation
- Optional composition status line, now disabled by default

## Included In 1.0.0

- Candidate ranking improvements for more natural phrase ordering
- Better handling for symbol-heavy and punctuation-heavy input
- Safer user personalization lifecycle and reset tooling
- Expanded regression coverage across layout variants and technical literals
- Release metadata and packaging synchronized to `v1.0.0`

## Upgrade Notes

- Existing user-learned personalization can be reset safely with:

  ```sh
  scripts/reset-user-data.sh
  ```

- If you previously installed a local override under `~/.local/lib/fcitx5`,
  remove or rename it so the system installation in `/usr/lib/fcitx5` is used.

## Installation

### Arch Linux

Download the package asset:

- `fcitx5-ari-ime-1.0.0-1-x86_64.pkg.tar.zst`

Install it with:

```sh
sudo pacman -U fcitx5-ari-ime-1.0.0-1-x86_64.pkg.tar.zst
fcitx5 -r
```

Then open `fcitx5-configtool` and add **Ari IME**.

### From Source

If you prefer to build from source:

```sh
git clone https://github.com/kaiyasi/Ari-IME.git
cd Ari-IME
git checkout v1.0.0
cmake -B build -DCMAKE_BUILD_TYPE=Release
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

> Ari IME 1.0.0 is the first stable release of a Fcitx5 Bopomofo input method
> built for mixed Chinese and English typing, smarter candidates, and safer
> per-user learning.

One-line store / release description:

> A stable Fcitx5 Bopomofo input method with mixed Chinese/English composition,
> context-aware candidates, and per-user learning.
