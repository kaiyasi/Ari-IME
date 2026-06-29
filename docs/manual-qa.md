# Manual QA Checklist

This checklist covers behavior that unit tests cannot prove because it depends
on real Fcitx5 UI, application toolkit preedit handling, clipboard integration,
and display server behavior.

## Setup

- Install the current build:

  ```sh
  scripts/check.sh
  sudo cmake --install build
  ```

- Restart Fcitx5.
- Add **知字 (Ari IME)** in `fcitx5-configtool`.
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

## Core Input

| Scenario | Steps | Expected |
|----------|-------|----------|
| Mixed Chinese/English | Type `aceru/6aj4`, press Enter | Preedit shows `acer螢幕`; app receives `acer螢幕` only after Enter |
| Literal English | Type `README.md`, press Enter | Preedit and commit stay `README.md` |
| URL/version literal | Type `https://ari-ime.test/v1.0.0`, press Enter | Version digits and dots stay literal |
| Acronym + Chinese | Type `HTTPsu3`, press Enter | Preedit and commit are `HTTP你` |
| Forced English | Press Ctrl+Space, type `su3`, press Enter | Mode hint shows English; commit is `su3`; mode remains English |

## Candidate Window

| Scenario | Steps | Expected |
|----------|-------|----------|
| Open candidates | Type `su3`, press Down | Candidate window opens on `你`; selected char is visually clear |
| Pick by number | With candidates open, press `2` | Candidate is applied; preedit updates; no premature commit |
| Pick by click/touch | Open candidates, click a visible candidate | Same result as number-key selection |
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
| Full-width punctuation | Enable full-width punctuation; type `< > ? ( ) { } ! : \ ^ @ % + =` | Preedit uses Chinese punctuation forms, including `、`, `……`, and full-width symbols |
| Bopomofo punctuation keys | Enable full-width punctuation; type `xu,4` | `,` remains a Bopomofo final key, not `，` |

## Visual Checks

- Candidate highlight is visible in the active Fcitx5 theme.
- The selected preedit character is identifiable while reselecting candidates.
- Aux up/down text does not overlap the candidate list.
- Long candidate labels such as `原始鍵 su3` are readable.
- The installed app icon appears in the input method list.
