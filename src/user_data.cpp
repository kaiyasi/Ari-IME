// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "user_data.h"

#include <cstdlib>

namespace inputer {

namespace {

std::filesystem::path configuredUserDataDir() {
    if (const char *overrideDir = std::getenv("INPUTER_USER_DATA_DIR");
        overrideDir && *overrideDir) {
        return std::filesystem::path(overrideDir);
    }
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "inputer";
    }
    if (const char *home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "inputer";
    }
    return {};
}

} // namespace

std::filesystem::path userDataDir() { return configuredUserDataDir(); }

std::filesystem::path userDictionaryPath() {
    const std::filesystem::path dir = userDataDir();
    if (dir.empty()) {
        return {};
    }
    return dir / "userdict.dat";
}

bool autoLearnEnabled() {
    return std::getenv("INPUTER_DISABLE_AUTOLEARN") == nullptr;
}

bool ensureUserDataDir(std::error_code &ec) {
    const std::filesystem::path dir = userDataDir();
    if (dir.empty()) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool resetUserDictionary(std::error_code &ec) {
    const std::filesystem::path path = userDictionaryPath();
    if (path.empty()) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }
    std::filesystem::remove(path, ec);
    return !ec;
}

} // namespace inputer
