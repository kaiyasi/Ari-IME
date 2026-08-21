# Ari IME 2.5.4

This patch release makes punctuation candidate selection follow the physical
key that produced the punctuation instead of a broad visual-variant catalog.

For each key, the candidate window now includes the reachable base and Shift
forms, the supported Ctrl/Shift and Alt shortcut outputs, and the reserved Alt
corner quotes. A left bracket therefore offers `[`, `{`, `「`, and `『`, while
unrelated symbols such as `!` and unreachable Chinese bracket variants stay
out. The same mapping is used for literal punctuation and punctuation-looking
Bopomofo keys, with native Chinese candidates first and raw-key recovery last.

The native and WebAssembly input cores retain the same punctuation behavior.
Validation covers the native regression suite, CTest, package metadata, and
the WebAssembly API smoke test.
