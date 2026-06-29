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
- Unified paging, Tab navigation, PageUp/PageDown, number-key selection, and
  click selection around the same candidate behavior.
- Implemented mid-string insertion, deletion, Home/End, and caret-driven
  editing.
- Labeled raw-key revert entries as `原始鍵 ...` to make their purpose clear.
- Normalized pasted control characters and separator-like Unicode codepoints so
  pasted text remains safe and visible inside a one-line preedit.
- Added forced-English mode, optional full-width punctuation, and proper
  numeric-keypad behavior.
- Expanded regression coverage for mixed literals such as email addresses,
  URLs, versions, file names, technical identifiers, and acronym-plus-Chinese
  composition.
- Improved English-tail peeling and reinterpretation logic so technical or
  symbol-heavy text is less likely to be rewritten as unintended Chinese.

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
- Added CI coverage for release, sanitizer, fuzz, and package simulation flows.

### Release And Packaging

- Synchronized addon metadata with the project version through CMake
  configuration.
- Installed addon descriptors, input-method descriptors, icons, and the Fcitx5
  shared module through the standard build.
- Kept PKGBUILD and `.SRCINFO` aligned with tagged release tarballs.
- Added version consistency checks across CMake, PKGBUILD, and `.SRCINFO`.

## Next Priorities

### P0: Real-World Compatibility

- Validate preedit, candidates, and caret behavior in at least one GTK, Qt,
  and Electron or browser application.
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

### P2: UX Refinement

- Continue evaluating whether post-selection caret retention feels natural in
  real editors.
- Add short release-quality demos or GIFs to the README for mixed input,
  reselection, paste handling, and layout switching.

### P3: Engineering Maintenance

- Keep refining CI cache strategy as runtime data becomes available.
- If layout probing cost grows, consider a more persistent in-process cache
  with explicit initialization tests.
