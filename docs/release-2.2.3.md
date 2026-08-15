# Ari IME 2.2.3

This patch release hardens the explicit first-run Fcitx5 profile setup.

`ari-ime-enable` now writes the standard `[GroupOrder]` section when it is
missing. Existing group-order entries are preserved, and a new entry is added
at the next available index rather than replacing an existing group. The
operation remains backed up, idempotent, and opt-in.
