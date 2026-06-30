# Changelog

## 1.1.0 - 2026-07-01

- Refined symbol-led Bopomofo handling so sequences such as `.3-3` can be
  recovered as valid Zhuyin without breaking punctuation-heavy literal input.
- Extended the `Up` reinterpretation path to handle symbol-led Zhuyin at word
  boundaries and after existing Chinese text.
- Improved candidate ranking to keep phrase-level Chinese context ahead of
  raw-key fallback when the surrounding text already forms a Chinese word.
- Added regression coverage for symbol-led reinterpretation, mixed
  Chinese-plus-symbol input, and phrase-preserving candidate order.

## 1.0.0 - 2026-06-29

- Promoted Ari IME to a 1.0.0 release with synchronized project, package, and
  addon version metadata.
- Improved candidate ranking with local context-aware heuristics layered on top
  of libchewing candidate pools.
- Reduced symbol-vs-Bopomofo ambiguity so punctuation-heavy input is less
  likely to produce awkward Chinese candidates.
- Added explicit user-data management utilities, including safe reset support
  for the learned user dictionary.
- Kept automated tests isolated from real user personalization data while
  preserving production auto-learning behavior.
- Added a dedicated user-data test suite covering reset behavior, base
  dictionary integrity, and restartable personalization.
- Made the auxiliary composition status line optional and disabled by default.
- Expanded regression coverage for technical literals, mid-string editing,
  punctuation behavior, and layout-specific symbol keys.
