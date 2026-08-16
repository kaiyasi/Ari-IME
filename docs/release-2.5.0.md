# Ari IME 2.5.0

This feature release adds safe reconversion for already-committed Chinese text
without introducing an Ari-specific window or background service.

In an application that exposes Fcitx5 surrounding text, select a short
all-Chinese range and press the configurable `ReconversionKey` (default
`Ctrl+Alt+R`). Ari temporarily reopens the selected text in its existing native
candidate editor, so a phrase can be corrected without deleting and retyping it.
Escape and focus reset restore the original selection; mixed, long, unsupported,
and sensitive selections leave the shortcut and application text untouched.

The first external reconversion may need to inspect libchewing's candidate
tables. Ari caches discovered readings per input context, while text already
composed by Ari uses its remembered readings immediately.

The release gate covers the full CTest suite, sanitizer checks, bounded fuzzing,
installation descriptors, and package simulation. Real application surrounding-
text behavior remains part of the GTK, Qt, and Electron checklist in
`docs/manual-qa.md`, because it depends on each frontend's Fcitx5 support.
