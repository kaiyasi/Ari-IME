# Ari IME 2.4.0

This feature release makes Chinese punctuation practical across desktop and
application shortcut environments without adding an Ari-specific UI.

The native Fcitx5 addon settings now expose `ChinesePunctuationShortcut` with
`Ctrl+Shift` as the default, plus `Alt+Shift`, `Ctrl`, `Alt`, and disabled
choices. The apostrophe key produces the Chinese enumeration comma `、` under
the selected gesture. Ordinary punctuation remains literal and half-width
when full-width punctuation is off.

An opt-in `SpaceCandidateMode` setting makes Space open candidates after a
complete Chinese conversion for users accustomed to traditional Zhuyin/Rime
flows. It is off by default, preserving Ari's mixed-input Space-as-tone-one or
literal-space behavior and Enter-only commit contract.

The release retains 2.3.7's durable explicit-choice learning, reliable
forgetting, and verified first-run Fcitx5 activation. Validation covers the
full regression suite, sanitizer tests, bounded fuzzing, package simulation,
and each punctuation modifier policy.
