// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#ifndef INPUTER_USER_DATA_H
#define INPUTER_USER_DATA_H

#include <filesystem>
#include <system_error>

namespace inputer {

// Directory holding per-user, mutable data for Ari IME. This contains only the
// user's learned/personalized state, not libchewing's built-in dictionary.
std::filesystem::path userDataDir();

// Path to the per-user libchewing dictionary that stores learned phrase and
// homophone preferences for Ari IME.
std::filesystem::path userDictionaryPath();

// Path to Ari's small, portable list of phrases the user explicitly selected,
// imported, or added through Ari's dictionary API. This is deliberately
// separate from libchewing's learned frequency database, whose entries cannot
// be distinguished from deliberate preferences through the old C API.
std::filesystem::path userPreferencePath();

// Whether automatic per-user learning is enabled for this process.
bool autoLearnEnabled();

// Ensure the per-user data directory exists and non-destructively seed
// libchewing 0.12's standard chewing.dat from Ari's legacy userdict.dat when
// needed. Returns false only when setup or migration fails.
bool ensureUserDataDir(std::error_code &ec);

// Remove Ari IME's learned user dictionary and explicit-preference sidecar.
// Missing files are treated as success. Built-in/system dictionary resources
// are untouched.
bool resetUserDictionary(std::error_code &ec);

} // namespace inputer

#endif // INPUTER_USER_DATA_H
