# Changelog

## Unreleased

## 2.2.0 - 2026-08-15

- Added an `AutoLearn` addon setting so users can keep the local personal
  dictionary unchanged without disabling candidate selection.
- Added the public-launch design and verification specification covering mixed
  input, candidate editing, privacy, packaging, and desktop compatibility.
- Made pasted Emoji grapheme clusters safe to move and delete as one unit,
  including CRLF paste normalization.
- Fixed the Fcitx5 addon descriptor to expose Ari's input method through its
  installed `inputer-im.conf` entry (`OnDemand=True`); Fcitx can now load the
  addon when Ari is selected instead of finding zero input methods.
- Added a native, display-only candidate preview for completed Chinese results;
  it shows contextual alternatives without taking numeric keys away from the
  next mixed 注音/English input.
- Hardened native candidate click handling so a delayed click from an older
  candidate page cannot select the same slot on a newer page.
- Added `scripts/install-local.sh` so a user-directory development install
  restarts Fcitx5 with the correct local addon path and verifies the loaded
  module instead of silently continuing to use an older system copy.

## 2.1.3 - 2026-08-14

- Candidate re-selection now keeps phrase recommendations that contain the
  focused character, including when opening candidates from the end of a word;
  Right can also move from the final candidate to the append position.

## 2.1.2 - 2026-08-14

- Made Space's tone-one decision consistently result-based for multi-key
  out-of-order syllables too; valid Han-producing sequences are no longer
  rejected merely because their raw letters resemble an English token.

## 2.1.1 - 2026-08-11

- Replaced the single-key tone-one input-category gate with an output-based
  decision: a key converts only when libchewing actually produces a Han
  character. The rule is covered across every supported keyboard layout, so
  literal keys such as `a` / `b` and valid conversions such as `u` -> `一` no
  longer depend on a hand-selected key list.

## 2.1.0 - 2026-08-11

- Added multi-level `Ctrl+Z` for recent candidate choices and `Shift+Delete` to
  forget only the highlighted personal dictionary entry.
- Prevented password and sensitive fields from writing per-user learning data.
- Added phrase-aware `Ctrl+Left` / `Ctrl+Right` navigation using libchewing's
  recognized intervals, with ordinary English word boundaries in mixed text.
- Enabled caret editing in forced-English mode and explicit `Ctrl+Shift`
  Chinese punctuation insertion at a mid-string caret.
- Made ordinary punctuation context-independent and half-width by default;
  `Ctrl+Shift` plus a punctuation key now requests the Chinese form explicitly,
  while the existing opt-in full-width mode remains available.
- Restored valid single-key tone-one syllables such as `u` + Space → `一`
  without regressing literal one-letter English tokens such as `a ` and `b `.
- Weighted unchanged conversions as weak positive learning evidence while
  giving explicit character and phrase selections roughly four times the
  learning weight plus a short local-context reinforcement pass.
- Restored libchewing's native conditional phrase scoring as the automatic
  context engine instead of overriding it with static first-candidate and
  sentence-specific correction rules.
- Added deterministic coverage for `我的` / `跑得快` contextual homophones and
  for one explicit phrase choice outweighing three unchanged commits.

## 2.0.3 - 2026-07-26

- Added an automated Arch x86_64 binary release pipeline that builds and tests
  each tag in a clean Arch container before publishing a stripped runtime
  archive and checksum to GitHub Releases.
- Enabled a `fcitx5-ari-ime-bin` AUR package with runtime dependencies only;
  source builds remain available directly from GitHub.

## 2.0.2 - 2026-07-26

- Optimized AUR build dependencies by relying on Arch's required `base-devel`
  environment instead of redundantly declaring `gcc` in `makedepends`.
- Kept runtime dependencies limited to Fcitx5, libchewing and the lightweight
  hicolor icon theme required by the installed input-method icon.

## 2.0.1 - 2026-07-26

- Added fully offline, high-confidence context correction for conversational
  homophones while preserving libchewing candidate restoration and learning.
- Added regression coverage for punctuation-to-Zhuyin boundaries such as
  `(hk4g4` -> `(測試` and contextual `你應該試試` conversion.
- Migrated legacy `userdict.dat` learning data non-destructively to
  libchewing 0.12's standard `chewing.dat` name and silenced expected
  first-run dictionary diagnostics.

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
