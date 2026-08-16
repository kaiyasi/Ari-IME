# Ari IME 2.3.4

This patch release keeps the personal-preference promotion from 2.3.3 while
removing its first-run overhead and fixing a cross-version regression in long
pre-edit candidate windows.

Ari now avoids enumerating libchewing's personal dictionary when the user has
not created one yet, then caches the phrase set for the lifetime of each input
context. Explicit imports and mappings added through the dictionary API still
take effect immediately. This keeps ordinary typing fast and avoids an older
libchewing compatibility path disturbing long candidate windows.

Validation includes release, sanitizer, package simulation, and bounded fuzz
checks, with regression coverage for long pre-edits and imported `妳`/`資`
personal mappings.
