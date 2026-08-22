# Ari IME 2.5.7

This release adds an opt-in pending-Zhuyin display, contributed by
@HongyiHank.

The new `ShowPendingZhuyin` addon setting (off by default) shows the Bopomofo
symbols of an incomplete pending syllable in the auxiliary line above the
caret while typing. Because Ari keeps the pre-edit as literal English until a
complete toned syllable forms, mid-syllable input otherwise looks identical to
ordinary English typing; the hint makes the composition state visible without
changing Ari's mixed-input behavior. The hint disappears once the syllable
converts, the input turns literal English, or candidate selection opens.

The implementation follows the existing layout-aware probe pattern: tone keys
are stripped per keyboard layout, and the probe context is reset on every
query, so no state leaks into live composition. The native and WebAssembly
cores build from the same source and stay aligned. Two comments contributed
with the feature were completed and corrected.

Validation covers the full CI matrix (GCC/Clang release builds, sanitizers,
bounded fuzzing, package simulation, Ubuntu/Debian packaging) plus manual
desktop testing by the contributor.
