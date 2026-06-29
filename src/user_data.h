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

// Whether automatic per-user learning is enabled for this process.
bool autoLearnEnabled();

// Ensure the per-user data directory exists. Returns false only when no usable
// path can be resolved or directory creation fails.
bool ensureUserDataDir(std::error_code &ec);

// Remove only Ari IME's learned user dictionary file. Missing files are treated
// as success. Built-in/system dictionary resources are untouched.
bool resetUserDictionary(std::error_code &ec);

} // namespace inputer

#endif // INPUTER_USER_DATA_H
