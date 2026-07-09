# Changelog

## 2.0.0 - 2026-07-09

- Degraded gracefully when the libchewing engine fails to initialize: retry
  against the read-only system dictionary, otherwise fall back to plain-English
  passthrough (with a one-time hint) instead of silently swallowing keys.
- Added an optional, user-configurable `FullWidthPunctuationToggle` shortcut to
  flip full-width punctuation live; unset by default so no application shortcut
  is reserved.
- Added Ubuntu/Debian support: apt dependency documentation, a `debian/`
  packaging directory, and a native Ubuntu build/test/package CI workflow.
- Documented known limitations in `ISSUES.md` and extended the version
  consistency check to cover `debian/changelog`.
- Closed a test-isolation gap: `TempConfigHome` now also sandboxes `HOME`,
  `XDG_DATA_HOME`, and `CHEWING_USER_PATH`, since libchewing 0.12 resolves its
  learned dictionary through those rather than only the path passed to
  `chewing_new2`. Tests are now deterministic regardless of the developer's
  real day-to-day typing.
- Pinned libchewing's learned dictionary to Ari's own data directory via
  `CHEWING_USER_PATH` (set only while the context is built), so learning no
  longer lands in — and pollutes — the shared `$XDG_DATA_HOME/chewing`
  directory used by other libchewing input methods.
- `scripts/reset-user-data.sh` now also clears libchewing's learned files
  (`chewing.dat`, `chewing-deleted.dat`), lists every file it will remove, and
  gained `--include-shared` to optionally reset the shared chewing directory
  left behind by older builds.

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
