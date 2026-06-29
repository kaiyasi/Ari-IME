# Ari IME 1.0.0

## Release Summary

Ari IME 1.0.0 is the first stable release of the project. It delivers mixed
Bopomofo and English composition without mode switching, context-aware
candidate handling, editable in-place preedit text, and per-user learning on
top of libchewing.

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

## Verification

Recommended local verification before publishing or packaging:

```sh
scripts/check.sh
```

## Assets / Copy

Short release copy:

> Ari IME 1.0.0 delivers stable mixed Bopomofo and English input, smarter
> candidate ranking, safer personalization, and a cleaner default UI.

One-line store / release description:

> A stable Fcitx5 Bopomofo input method with mixed Chinese/English composition,
> context-aware candidates, and per-user learning.
