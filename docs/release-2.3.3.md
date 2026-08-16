# Ari IME 2.3.3

This release makes personal preferences reliable across libchewing versions.
When a candidate is already present in Ari's personal dictionary, Ari promotes
that explicit or imported mapping in the live result. This keeps deliberate
choices such as a homophone or a one-key tone-one mapping useful on older
distributions whose libchewing frequency scorer does not rank user phrases
first.

The built-in dictionary and its contextual ranking remain owned by libchewing;
Ari does not replace ordinary sentence-specific candidates with a static word
list. `ari-ime-dict candidates` now reports the same promoted result as the
interactive input path.

Validation includes release, sanitizer, package simulation, and bounded fuzz
checks, plus regression coverage for imported `妳`, imported `資` from `y` +
Space, and cross-version candidate availability.
