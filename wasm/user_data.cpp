// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi

#include "user_data.h"

#include <filesystem>

namespace inputer {
namespace {

std::filesystem::path dataDir() {
#ifdef INPUTER_WASM_NATIVE_TEST
    return std::filesystem::temp_directory_path() / "ari-ime-wasm-native";
#else
    // Emscripten's MEMFS keeps this state in the module instance. A host can
    // persist the files through the module's FS API without Ari knowing about
    // browser storage, Node paths, or any other host-specific persistence.
    return "/ari-ime";
#endif
}

} // namespace

std::filesystem::path userDataDir() { return dataDir(); }

std::filesystem::path userDictionaryPath() {
    return dataDir() / "userdict.dat";
}

std::filesystem::path userPreferencePath() {
    return dataDir() / "preferences.tsv";
}

bool autoLearnEnabled() {
    return true;
}

bool ensureUserDataDir(std::error_code &ec) {
    std::filesystem::create_directories(dataDir(), ec);
    return !ec;
}

bool resetUserDictionary(std::error_code &ec) {
    ec.clear();
    for (const auto &name : {"userdict.dat", "uhash.dat", "chewing.dat",
                             "chewing-deleted.dat", "preferences.tsv"}) {
        std::filesystem::remove(dataDir() / name, ec);
        if (ec) {
            return false;
        }
    }
    return true;
}

} // namespace inputer
