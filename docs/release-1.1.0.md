# Ari IME 1.1.0

## Release Summary

Ari IME 1.1.0 improves how the engine handles ambiguous symbol-led input. It
keeps punctuation-heavy text literal when that is clearly what the user meant,
while restoring valid symbol-led Zhuyin composition when the surrounding input
shows real Bopomofo intent.

## Short Description

Ari IME is a Fcitx5 input method for Traditional Chinese that supports mixed
Bopomofo and English composition, context-aware candidates, editable in-place
preedit text, and per-user learning on top of libchewing.

## Highlights

- Better symbol-vs-Zhuyin disambiguation for punctuation-heavy typing
- Improved `Up` reinterpretation for symbol-led Zhuyin such as `.3-3`
- Candidate ranking that better preserves Chinese phrase context
- Continued protection for technical literals such as filenames, URLs, and
  version strings
- Regression coverage for boundary punctuation, reinterpretation, and phrase
  ordering

## Included In 1.1.0

- Symbol-led Zhuyin recovery from boundary-literal staging
- Phrase-aware ranking updates for candidate and raw-key fallback ordering
- Better reinterpretation after existing Chinese text
- Expanded test coverage for punctuation-heavy and mixed-context input
- Release metadata and packaging synchronized to `v1.1.0`

## Upgrade Notes

- Existing user-learned personalization remains compatible.
- If you previously installed a local override under `~/.local/lib/fcitx5`,
  remove or rename it so the system installation in `/usr/lib/fcitx5` is used.
- Restart Fcitx5 after installing the updated package:

  ```sh
  fcitx5 -r
  ```

## Installation

### Arch Linux

Download the package asset:

- `fcitx5-ari-ime-1.1.0-1-x86_64.pkg.tar.zst`

Install it with:

```sh
sudo pacman -U fcitx5-ari-ime-1.1.0-1-x86_64.pkg.tar.zst
fcitx5 -r
```

Then open `fcitx5-configtool` and add **Ari IME**.

### From Source

If you prefer to build from source:

```sh
git clone https://github.com/kaiyasi/Ari-IME.git
cd Ari-IME
git checkout v1.1.0
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

> Ari IME 1.1.0 improves punctuation-heavy and symbol-led Bopomofo typing with
> better phrase-aware ranking, safer reinterpretation, and cleaner literal
> fallback behavior.

One-line store / release description:

> A Fcitx5 Bopomofo input method with mixed Chinese/English composition,
> context-aware candidates, and improved symbol-led Zhuyin handling.
