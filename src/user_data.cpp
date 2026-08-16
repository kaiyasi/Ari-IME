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

std::filesystem::path userPreferencePath() {
    const std::filesystem::path dir = userDataDir();
    if (dir.empty()) {
        return {};
    }
    return dir / "preferences.tsv";
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
    if (ec) {
        return false;
    }

    // libchewing 0.12 consults CHEWING_USER_PATH/chewing.dat even when Ari
    // supplies its historical userdict.dat explicitly. Preserve the old file
    // and seed the standard name on upgrade so existing learning is retained
    // without a noisy "Dictionary file not found" diagnostic.
    const std::filesystem::path legacy = dir / "userdict.dat";
    const std::filesystem::path standard = dir / "chewing.dat";
    std::error_code inspectEc;
    if (!std::filesystem::exists(standard, inspectEc) && !inspectEc &&
        std::filesystem::exists(legacy, inspectEc) && !inspectEc) {
        std::filesystem::copy_file(legacy, standard,
                                   std::filesystem::copy_options::none, ec);
        if (ec == std::errc::file_exists) {
            ec.clear(); // another context completed the same migration
        }
    }
    return !ec;
}

bool resetUserDictionary(std::error_code &ec) {
    const std::filesystem::path dir = userDataDir();
    if (dir.empty()) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    const std::filesystem::path paths[] = {
        dir / "userdict.dat",
        dir / "chewing.dat",
        dir / "chewing-deleted.dat",
        dir / "preferences.tsv",
    };
    ec.clear();
    for (const auto &path : paths) {
        std::filesystem::remove(path, ec);
        if (ec) {
            return false;
        }
    }
    return !ec;
}

} // namespace inputer
