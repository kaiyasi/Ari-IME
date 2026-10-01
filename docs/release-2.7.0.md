# Ari IME 2.7.0

This release strengthens personal learning and preserves the exact key order
when reverting an out-of-order Bopomofo syllable.

## Learning

- AutoLearn and sensitive-field settings now also control libchewing's own
  learner during candidate selection.
- Ari trains a reading only when the replayed phrase exactly matches what was
  committed, preventing an unavailable candidate from teaching the wrong text.
- Explicit preferences are saved after successful learning and updated under
  a file lock, so concurrent input contexts retain each other's choices.

## Input correction

- Reverting an out-of-order syllable preserves its typed key sequence. Typing
  `240` and reverting to raw keys now returns `240`.

## Packages

The GitHub release builds an Arch binary archive and a Debian package. The
WebAssembly package stays at 2.6.4 because its checked-in runtime requires a
separate Emscripten rebuild before it can include these changes.
