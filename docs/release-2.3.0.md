# Ari IME 2.3.0

This release makes personal phrase learning portable without adding another
user interface.

The package now installs `ari-ime-dict`, which can report the active libchewing
version and data directory, print reproducible candidates for a raw 大千 key
sequence, export personal phrase mappings as readable UTF-8, validate and
merge an import, and create a raw-data backup. The portable format stores
canonical Unicode Bopomofo readings such as `ㄋㄧˇ`, so it is independent of the
keyboard layout used to type the phrase.

Imports are idempotent and create a timestamped backup before existing data is
changed. Exact frequency and deleted-entry state can still vary across
libchewing versions; use `ari-ime-dict backup` when same-engine raw recovery is
needed.
