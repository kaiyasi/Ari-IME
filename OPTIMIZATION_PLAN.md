# Ari IME Optimization Plan

This document summarizes the optimization work already landed in the project
and the next improvements worth pursuing. The categories focus on input-method
usability first: feature coverage, UX, performance, and engineering quality.

## Completed

### Input Method Functionality

- Added support for multiple Bopomofo keyboard layouts, including Default,
  Eten, Hsu, IBM, Gin-Yieh, Dvorak, Carpalx, Colemak-DH, Workman, and Colemak.
- Consolidated layout handling into a shared layer so the state machine and
  libchewing keyboard type stay synchronized.
- Preserved mixed Chinese/English preedit text until Enter commits it, instead
  of leaking partial candidates or English fragments into the application.
- Enabled candidate reselection anywhere in the active preedit, including
  phrase picks, single-character picks, raw-key reversion, and click/touch
  selection.
- Kept live preedit text aligned with the first candidate shown in the
  selection window.
- Added a native display-only live recommendation list for completed Chinese
  runs; it previews the current phrase without turning the next digit into a
  candidate-selection command.
- Unified paging, Tab navigation, PageUp/PageDown, number-key selection, and
  click selection around the same candidate behavior.
- Rejected delayed candidate clicks whose visible text no longer matches the
  current page, preventing a stale frontend callback from selecting the wrong
  homophone.
- Implemented mid-string insertion, deletion, Home/End, and caret-driven
  editing.
- Labeled raw-key revert entries as `原始鍵 ...` to make their purpose clear.
- Normalized pasted control characters and separator-like Unicode codepoints so
  pasted text remains safe and visible inside a one-line preedit.
- Kept common Unicode grapheme clusters intact during paste, caret movement and
  deletion, including ZWJ emoji, variation selectors and regional-indicator
  flags.
- Added forced-English mode, optional full-width punctuation, and proper
  numeric-keypad behavior.
- Expanded regression coverage for mixed literals such as email addresses,
  URLs, versions, file names, technical identifiers, and acronym-plus-Chinese
  composition.
- Improved English-tail peeling and reinterpretation logic so technical or
  symbol-heavy text is less likely to be rewritten as unintended Chinese.
- Added the package-installed `ari-ime-dict` command for personal phrase
  diagnostics, reproducible candidate inspection, readable export/import, and
  raw-data backups without adding a GUI.
- Made portable dictionary imports validate the complete file first, merge
  idempotently, and preserve existing libchewing data before mutation.
- Promote explicit/imported personal phrase mappings in Ari's live result, so
  older libchewing frequency implementations do not silently demote a user's
  deliberate preference; built-in contextual candidates remain untouched.

### UX

- Added auxiliary-line status display for language mode, keyboard layout, and
  punctuation mode, with the status line now optional and disabled by default.
- Added transient hints for layout switches and full-width punctuation changes.
- Surfaced candidate page counts in the auxiliary line.
- Kept candidate click behavior aligned with number-key behavior.
- Preserved caret position across stale clicks, raw-key reversion, page jumps,
  deletes, and consecutive reselection flows.
- Added an SVG icon so the input method no longer appears blank in tooling.
- Strengthened full-width punctuation fallback coverage across layouts and
  symbol-looking Bopomofo keys.
- Added `docs/manual-qa.md` to standardize real desktop-session validation.

### Performance And Stability

- Kept layout slot-table detection lazy so each layout is probed only when
  needed.
- Centralized visible candidate count calculations to avoid rebuilding
  candidate views unnecessarily.
- Isolated test dictionaries from real user-learned data.
- Added warning flags, sanitizer support, a local `scripts/check.sh` pipeline,
  coverage support, and fuzzing support.
- Added deterministic stress tests and dedicated user-data reset/learning
  tests.
- Added an `AutoLearn` switch that leaves candidate selection available while
  keeping the personal dictionary unchanged.
- Added CI coverage for release, sanitizer, fuzz, and package simulation flows.

### Release And Packaging

- Synchronized addon metadata with the project version through CMake
  configuration.
- Installed addon descriptors, input-method descriptors, icons, and the Fcitx5
  shared module through the standard build.
- Kept PKGBUILD and `.SRCINFO` aligned with tagged release tarballs.
- Added version consistency checks across CMake, PKGBUILD, and `.SRCINFO`.
- Added a local-install helper that restarts Fcitx5 with the user addon path and
  verifies that the newly built module is actually loaded.
- Added the package-installed `ari-ime-enable` command for an explicit,
  backup-producing first-run profile setup, plus `ari-ime-reset-data` so
  package users can reset learning without a source checkout.
- Installed `ari-ime-dict` alongside the input method and covered its binary,
  parser, import/export, candidate restoration, and backup behavior in the
  release gate.
- Increased the live libchewing context window from 20 to 32 Chinese characters
  so longer sentences retain useful context before internal replay/chunking.

## Current Release Audit

- A real GTK3 `GtkIMContext` smoke path passed under Wayland, including
  preedit and commit.
- A real Qt6 `QLineEdit` smoke path passed under X11, including preedit and
  commit.
- A VS Code Electron editor smoke path passed under X11.
- The remaining matrix in [docs/manual-qa.md](docs/manual-qa.md) is still kept
  as an application/theme checklist; toolkit smoke success does not imply
  every GTK, Qt, browser, or Fcitx5 theme behaves identically.

## Next Priorities

### P0: Real-World Compatibility

- Repeat the core scenarios in a named GTK and Qt editor when those applications
  are available on the target distribution, then record theme-specific issues.
- Verify auxiliary text, highlight visibility, and page indicators across
  different Fcitx5 themes.
- Confirm clipboard, click, and keypad behavior under both Wayland and X11.

### P1: Input Behavior

- Keep extending punctuation regressions, especially for layouts with
  symbol-shaped Bopomofo keys.
- If a full-width punctuation shortcut is added later, make it configurable
  rather than hardcoded so application shortcuts remain safe.
- Continue broadening reinterpretation regressions with more real-world
  developer and document-authoring text samples.
- Use `ari-ime-dict candidates` plus a small versioned golden corpus when a
  distribution changes libchewing candidate ordering; keep the golden corpus
  for built-in results while treating explicit personal mappings as Ari-owned
  preferences rather than hardcoding sentence-specific replacements.
- Evaluate an optional traditional Space-to-commit compatibility policy in
  real editors before changing Ari's current whole-preedit Enter contract.

### P2: UX Refinement

- Continue evaluating whether post-selection caret retention feels natural in
  real editors.
- Add short release-quality demos or GIFs to the README for mixed input,
  reselection, paste handling, and layout switching.
- Investigate reconversion of already-committed selected text when the client
  exposes sufficient Fcitx5 surrounding-text support.

### P3: Engineering Maintenance

- Keep refining CI cache strategy as runtime data becomes available.
- If layout probing cost grows, consider a more persistent in-process cache
  with explicit initialization tests.
