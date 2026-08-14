# Known Limitations

This file tracks known limitations and environment-dependent behavior that are
not bugs in Ari IME itself but affect how it behaves in the field. See
[CHANGELOG.md](CHANGELOG.md) for fixed issues and release history.

## libchewing version sensitivity

Ari IME does not own libchewing's exact candidate ordering. Different libchewing
versions can rank homophones/phrases differently, so the *first* candidate shown
may vary between distributions:

- The Arch release gate uses the current Arch libchewing package and prints its
  resolved version in the build log; the exact version can change independently
  of Ari.
- Ubuntu/Debian build against whatever the distribution ships (for example
  **libchewing 0.8.x** on Ubuntu 24.04).

The automated tests are written to tolerate ranking differences where Ari IME
does not control the order. The Ubuntu CI job (`.github/workflows/ubuntu.yml`)
prints the resolved `chewing` version so ordering differences can be correlated
with dependency drift.

## Engine initialization failure is degraded, not fatal

If the libchewing context cannot be created (missing system dictionary, or an
unwritable user-dictionary directory), Ari IME:

1. retries with a read-only chewing context (losing only per-user learning), and
2. if that still fails, degrades to plain-English passthrough so keystrokes stay
   visible instead of being silently swallowed, showing a one-time hint
   (「注音引擎載入失敗，暫以英文輸入」).

Set `CHEWING_PATH` if libchewing's system dictionary is in a non-standard
location.

## Learned-dictionary location (libchewing 0.12+)

Recent libchewing versions store learned phrases (`chewing.dat`,
`chewing-deleted.dat`) at
`CHEWING_USER_PATH`, falling back to `$XDG_DATA_HOME/chewing` — not at the
`userpath` file passed to `chewing_new2`. Ari pins `CHEWING_USER_PATH` to its own
data directory (`~/.config/inputer` by default) so learning stays self-contained
and does not pollute the shared chewing directory used by other libchewing input
methods (fcitx5-chewing, ibus-chewing).

Older Ari builds did not pin this, so learned data may still sit in
`~/.local/share/chewing`. `scripts/reset-user-data.sh --include-shared` clears
that shared location; without the flag it only prints a notice, since the
directory may be shared. Reset the learned dictionary with
`scripts/reset-user-data.sh`.

## Real-application rendering depends on the desktop session

Pre-edit display, candidate windows, clipboard behavior, and theme rendering are
provided by the client toolkit and Fcitx5 theme, not by Ari IME. These still
need manual validation across toolkits and display servers; the matrix and steps
live in [docs/manual-qa.md](docs/manual-qa.md). Current status of that matrix is
tracked there.

## Pinyin layouts are intentionally unsupported

The state machine is built around one-key-per-Bopomofo-symbol layouts, so
Pinyin keyboard modes are deliberately not exposed.
