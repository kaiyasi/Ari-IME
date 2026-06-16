// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#ifndef INPUTER_CONSTANTS_H
#define INPUTER_CONSTANTS_H

namespace inputer {

// Candidates shown per page. Must stay in sync across three places: chewing's
// candPerPage, the UI candidate list page size / selection keys, and the
// selection-mode paging math in Buffer.
inline constexpr int kCandPerPage = 9;

// Upper bound on the number of syllables in one phrase interval. Used as a loop
// guard when descending chewing's phrase intervals (longest phrase down to
// single characters).
inline constexpr int kMaxSyllables = 8;

} // namespace inputer

#endif // INPUTER_CONSTANTS_H
