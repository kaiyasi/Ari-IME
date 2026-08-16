# Ari IME 2.3.6

This patch release fixes a compatibility regression in personal phrase
promotion on older libchewing releases.

Older libchewing C APIs expose ordinary learned frequencies and explicit user
phrase mappings through the same enumeration. Ari now keeps only mappings
explicitly imported or added through its dictionary API in
`preferences.tsv`. Ordinary learning remains under libchewing's own scorer, so
long pre-edit windows are no longer rewritten by promoting unrelated learned
entries.

The Ari preference sidecar is included in dictionary backups and is removed by
the learned-data reset command. Validation covers native libchewing, a locally
built libchewing 0.6 compatibility build, sanitizers, packaging, and bounded
fuzzing.
