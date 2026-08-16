# Ari IME 2.3.7

This release makes deliberate personalization and first-run activation
reliable across real input contexts.

When AutoLearn is enabled, a phrase explicitly selected during composition is
stored in Ari's small `preferences.tsv` sidecar. It is promoted on later
compositions without treating the entire libchewing learned-frequency database
as a hard-priority list. `Shift+Delete` now removes that marker as well as the
matching personal entry, so a forgotten choice stays forgotten after restart.

The installed `ari-ime-enable --make-default` command now reloads a running
Fcitx5, starts it when a graphical session is available, selects `inputer`, and
verifies the active name. Headless shells receive an explicit next-step error
instead of a false success.

Validation covers native libchewing, sanitizer tests, bounded fuzzing, profile
runtime mocks, packaging metadata, and the existing mixed-input/candidate
regression suite.
