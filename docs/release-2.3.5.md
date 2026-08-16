# Ari IME 2.3.5

This patch release keeps personal phrase preferences reliable while making
their compatibility path safe for older libchewing versions.

Ari now enumerates the personal dictionary once when each libchewing context
is created, before any pre-edit exists. It does not enumerate the dictionary
in the middle of typing, which avoids disturbing long candidate windows on
older distributions. Mappings added through Ari's dictionary API still take
effect immediately; changes made by another process take effect after Fcitx5
is restarted.

Validation includes release, sanitizer, package simulation, and bounded fuzz
checks, plus regression coverage for long pre-edits and imported `妳`/`資`
personal mappings.
