# Manual QA Checklist

This checklist covers behavior that unit tests cannot prove because it depends
on real Fcitx5 UI, application toolkit preedit handling, clipboard integration,
and display server behavior.

## Setup

- Install the current build:

  ```sh
  scripts/check.sh
  INPUTER_BUILD_DIR=build bash scripts/install-local.sh
  ```

  For a system/package install, use the distribution package or
  `sudo cmake --install build` instead. The local helper is important when
  testing a `~/.local` build because Fcitx5 may otherwise keep loading the
  older system addon.

- Restart Fcitx5.
- Add **Ari IME** in `fcitx5-configtool`.
- Select it and verify that the running daemon uses it:

  ```sh
  fcitx5-remote -s inputer
  test "$(fcitx5-remote -n)" = inputer
  ```

- Test with a fresh learned dictionary when validating deterministic behavior:

  ```sh
  INPUTER_DISABLE_AUTOLEARN=1 fcitx5 -r
  ```

- Candidate ordering comes from libchewing and may change across libchewing
  versions. When a scenario inserts `1j4`, validate caret placement and commit
  behavior using the current default ㄅㄨˋ candidate shown by your system.

## Environment Matrix

Run the core scenarios in at least one app from each toolkit group:

| Group | Suggested apps | Done |
|-------|----------------|------|
| GTK | gedit, GNOME Text Editor, Firefox text field | [ ] |
| Qt | Kate, KWrite, Qt Creator text field | [ ] |
| Electron / Chromium | Chromium, VS Code, Discord text field | [ ] |

Run clipboard and candidate-click scenarios under both display servers when
available:

| Display server | Done |
|----------------|------|
| Wayland | [ ] |
| X11 | [ ] |

The current public-release audit also includes these recorded toolkit
smoke paths (they validate the Fcitx5 input-context protocol, preedit and
commit path; they do not replace the named-application and theme checks
below):

| Toolkit path | Display server | Result |
|--------------|----------------|--------|
| GTK3 `GtkIMContext` preedit/commit harness | Wayland | Passed |
| Qt6 `QLineEdit` preedit/commit harness | X11 | Passed |
| VS Code Electron editor preedit/commit smoke | X11 | Passed |

If a release is tested on a different desktop session, rerun the matrix above
and record any application-specific behavior before publishing it.

## Core Input

| Scenario | Steps | Expected |
|----------|-------|----------|
| Mixed Chinese/English | Type `aceru/6aj4`, press Enter | Preedit shows `acer螢幕`; app receives `acer螢幕` only after Enter |
| Literal English | Type `README.md`, press Enter | Preedit and commit stay `README.md` |
| Tone-one result decision | On the default layout, type `a` then Space, `b` then Space, `u` then Space, and an out-of-order body such as `ia` then Space in fresh preedits | `a ` and `b ` stay literal; `u` + Space and valid Han-producing bodies such as `ia` become Chinese, based on actual conversion output rather than a key or word list |
| URL/version literal | Type `https://ari-ime.test/v1.1.0`, press Enter | Version digits and dots stay literal |
| Acronym + Chinese | Type `HTTPsu3`, press Enter | Preedit and commit are `HTTP你` |
| Forced English | Press Ctrl+Space, type `su3`, press Enter | Mode hint shows English; commit is `su3`; mode remains English |

## Candidate Window

| Scenario | Steps | Expected |
|----------|-------|----------|
| Live recommendation | Type `hk4g4` (`測試`) without pressing Down | Native Fcitx5 panel previews `測試` and alternatives; the first item matches the visible result |
| Recommendation keeps typing | Type `hk4g4su3` while the preview is visible | Digits still form the next syllable; preedit becomes `測試你`, not a candidate pick |
| Open candidates | Type `su3`, press Down | Candidate window opens on `你`; selected char is visually clear |
| Trailing phrase recommendation | Type `hk4g4` (`測試`), press Down at the end | The candidate list recommends `測試` before single-character `試` alternatives |
| Pick by number | With candidates open, press `2` | Candidate is applied; preedit updates; no premature commit |
| Pick by click/touch | Open candidates, click a visible candidate | Same result as number-key selection |
| Continue after pick | Re-pick a character in a long preedit, then type another syllable | Candidate mode closes and new text appends at the end without extra Right/End keys |
| Undo candidate choice | Pick two candidates, press Ctrl+Z twice | Each press restores one choice; after typing or deleting text, Ctrl+Z returns to the application |
| Forget personal candidate | Highlight a previously learned candidate, press Shift+Delete | A transient success hint appears; a fresh composition no longer promotes that personal choice, while it remains selectable from the base dictionary |
| Page candidates | Type `su3`, press Down, PageDown/PageUp | Candidate page changes; aux line shows page count |
| Raw key revert | Type `su3`, press Down, Shift+Tab or Up to `原始鍵 su3`, press Enter | Preedit becomes `su3` |
| Stale click resilience | Open candidates, rapidly page then click an old candidate position | Candidate window does not disappear unexpectedly |

## Caret Editing

| Scenario | Steps | Expected |
|----------|-------|----------|
| Insert before text | Type `su3cl3`, press Home, type `1j4` | The current default ㄅㄨˋ candidate appears before `你好`; caret remains after inserted char |
| Insert in middle | Type `su3cl3`, press Left, type `1j4` | The current default ㄅㄨˋ candidate appears between `你` and `好` |
| Delete right of caret | Type `su3cl3`, press Left, Delete | Preedit becomes `你` |
| Close candidates | Type `su3`, Down, Esc, type `1j4` | Candidate window closes; next input inserts at caret |
| Append after candidate selection | Type `su3cl3`, Down, Right, type `1j4` | Right leaves the final candidate and places the caret after `你好`; new text appends there |
| Long preedit position | Compose more than 30 characters, then use Home/Left/Right and open candidates | Auxiliary lines show nearby text plus `游標 n/N` or `選字 n/N` |
| Phrase navigation | Compose a Chinese phrase and mixed English words, then use Ctrl+Left/Ctrl+Right | Caret jumps by libchewing phrase intervals and English word boundaries; Ctrl+Shift+Arrow remains available to the application |
| Forced-English editing | Toggle forced English, compose literal text, then use arrows, Backspace and mid-string typing | Literal preedit can be edited normally and Up does not reinterpret it as Zhuyin |

## Clipboard

| Scenario | Steps | Expected |
|----------|-------|----------|
| Paste into empty preedit | Copy `ABC`, press Ctrl+V | Preedit becomes `ABC`; typing continues after paste |
| Paste at caret | Type `su3cl3`, press Left, copy `ABC`, press Ctrl+V | Preedit becomes `你ABC好` |
| Paste multiline | Copy text with tabs/newlines, press Ctrl+V | Control separators become visible spaces in one-line preedit |
| Shift+Insert | Copy `ABC`, press Shift+Insert | Same as Ctrl+V |
| Shift+KP_Insert | Copy `ABC`, press Shift+numeric-keypad Insert | Same as Ctrl+V |

## Numeric Keypad

| Scenario | Steps | Expected |
|----------|-------|----------|
| NumLock digits | Press keypad `1`, `2`, `3` | Preedit is literal `123`; digits do not become tone keys |
| Keypad navigation | Type `su3cl3`, press keypad Home, type `1j4` | The current default ㄅㄨˋ candidate appears before `你好` |
| Keypad candidate paging | Type `su3`, keypad Down, keypad PageDown/PageUp | Candidate pages move like main keyboard keys |

## Configuration And Status

| Scenario | Steps | Expected |
|----------|-------|----------|
| Layout switch | Change keyboard layout in configtool while preedit is active | Preedit clears; transient keyboard-layout hint appears |
| Full-width punctuation toggle | Toggle full-width punctuation in configtool | Transient `標點 全形` / `標點 半形` hint appears |
| App shortcut passthrough | Press common app shortcuts such as Ctrl+. in VS Code or browser text fields | The app shortcut still works; Ari IME does not reserve a fixed punctuation toggle shortcut |
| Status line | Compose any non-empty preedit | Aux line shows 中/英, keyboard layout, punctuation mode |
| Literal punctuation | With full-width punctuation off, type `su3`, then `< > ?`; start a fresh preedit and type `API?` | Punctuation does not depend on language context: results are `你<>?` and `API?` |
| Full-width punctuation | Enable full-width punctuation; type `< > ? ( ) { } ! : \ ^ ' @ % + =` | Preedit uses Chinese punctuation forms, including `、`, `……`, and full-width symbols |
| Explicit Chinese punctuation | With full-width punctuation off, use Ctrl+Shift with comma, period, slash and the apostrophe key (`'`) | Preedit adds `，。？、`; specifically `Ctrl+Shift+'` produces `、`; Ctrl+' and Alt punctuation remain available to the application |
| Mid-string Chinese punctuation | Compose `你好`, move the caret between the characters, then press Ctrl+Shift+comma | Preedit becomes `你，好` |
| Bopomofo punctuation keys | Enable full-width punctuation; type `xu,4` | `,` remains a Bopomofo final key, not `，` |

## Per-user Learning

| Scenario | Steps | Expected |
|----------|-------|----------|
| Learn a homophone | With learning enabled and fresh user data, type `su3`, choose `妳`, press Enter, then type `su3` again | `妳` becomes the default conversion on the next composition |
| Weak versus strong evidence | With fresh data, type and commit `su3cl3` three times unchanged; then choose and commit the phrase `妳好` once; type `su3cl3` again | The one explicit phrase choice outweighs the three accepted defaults and produces `妳好` |
| Automatic context | With fresh data, type `ji32k7`, then type `ql32k7dj94` in a fresh preedit | The same ㄉㄜ˙ reading follows context: `我的` and `跑得快` |
| Sensitive field | In a password/sensitive field, choose a non-default candidate and commit; repeat in a normal field | The sensitive choice is not learned; the normal-field choice still is |
| Auto-learning toggle | Disable `AutoLearn` in addon settings, choose and commit a candidate, then repeat in a normal field | The choice is not learned while disabled; re-enabling restores ordinary learning |
| Commit boundary | Choose a non-default candidate, then reset the input method before pressing Enter | The uncommitted choice is not learned |

## Visual Checks

- Candidate highlight is visible in the active Fcitx5 theme.
- The selected preedit character is identifiable while reselecting candidates.
- Aux up/down text does not overlap the candidate list.
- Long candidate labels such as `原始鍵 su3` are readable.
- The installed app icon appears in the input method list.
