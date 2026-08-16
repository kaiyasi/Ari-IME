// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "user_data.h"
#include "zhuyin.h"

namespace {

constexpr std::string_view kHeader = "# Ari IME user dictionary v1";

#ifndef INPUTER_CHEWING_VERSION
#define INPUTER_CHEWING_VERSION "unknown"
#endif

struct Entry {
    std::string phrase;
    std::string reading;
};

std::string entryKey(std::string_view phrase, std::string_view reading) {
    std::string key(phrase);
    key.push_back('\t');
    key.append(reading);
    return key;
}

void usage(std::ostream &out) {
    out << "Usage:\n"
        << "  ari-ime-dict info\n"
        << "  ari-ime-dict list\n"
        << "  ari-ime-dict export [FILE|-]\n"
        << "  ari-ime-dict import [--dry-run] FILE|-\n"
        << "  ari-ime-dict backup\n"
        << "  ari-ime-dict candidates KEYS\n\n"
        << "Export/import uses a readable UTF-8 format with canonical Bopomofo\n"
        << "readings (for example ㄋㄧˇ, not layout-specific keys such as su3).\n"
        << "Import merges entries\n"
        << "and creates a timestamped backup before changing Ari's data.\n";
}

bool hasTabOrNewline(std::string_view value) {
    return value.find('\t') != std::string_view::npos ||
           value.find('\n') != std::string_view::npos ||
           value.find('\r') != std::string_view::npos;
}

bool readEntries(std::istream &in, std::vector<Entry> &entries,
                 std::string &error) {
    std::string line;
    std::size_t lineNumber = 0;
    bool headerSeen = false;
    std::unordered_set<std::string> seen;

    while (std::getline(in, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            if (line == kHeader) {
                headerSeen = true;
            }
            continue;
        }
        if (!headerSeen) {
            error = "missing Ari IME dictionary header before line " +
                    std::to_string(lineNumber);
            return false;
        }

        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size() ||
            line.find('\t', tab + 1) != std::string::npos) {
            error = "expected phrase<TAB>reading on line " +
                    std::to_string(lineNumber);
            return false;
        }
        const std::string phrase = line.substr(0, tab);
        const std::string reading = line.substr(tab + 1);
        if (hasTabOrNewline(phrase) || hasTabOrNewline(reading)) {
            error = "tab/newline is not allowed inside an entry on line " +
                    std::to_string(lineNumber);
            return false;
        }

        // Deduplicate before touching the live dictionary. The separator is
        // not valid inside either field, so this key is unambiguous.
        if (seen.insert(entryKey(phrase, reading)).second) {
            entries.push_back({phrase, reading});
        }
    }
    if (!in.eof()) {
        error = "could not read the dictionary file";
        return false;
    }
    if (!headerSeen) {
        error = "missing Ari IME dictionary header";
        return false;
    }
    return true;
}

bool writeEntries(std::ostream &out, std::vector<UserPhrase> entries) {
    std::sort(entries.begin(), entries.end(),
              [](const UserPhrase &a, const UserPhrase &b) {
                  if (a.phrase != b.phrase) {
                      return a.phrase < b.phrase;
                  }
                  return a.reading < b.reading;
              });

    out << kHeader << '\n'
        << "# One entry per line: phrase<TAB>canonical Bopomofo reading\n";
    for (const auto &entry : entries) {
        if (entry.phrase.empty() || entry.reading.empty() ||
            hasTabOrNewline(entry.phrase) || hasTabOrNewline(entry.reading)) {
            return false;
        }
        out << entry.phrase << '\t' << entry.reading << '\n';
    }
    return static_cast<bool>(out);
}

std::vector<std::filesystem::path> dictionaryFiles() {
    const auto dir = inputer::userDataDir();
    return {dir / "userdict.dat", dir / "chewing.dat",
            dir / "chewing-deleted.dat", dir / "preferences.tsv"};
}

std::filesystem::path makeBackup(std::error_code &ec) {
    ec.clear();
    const auto dir = inputer::userDataDir();
    if (dir.empty()) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return {};
    }

    std::vector<std::filesystem::path> existing;
    for (const auto &file : dictionaryFiles()) {
        std::error_code inspectEc;
        if (std::filesystem::is_regular_file(file, inspectEc) && !inspectEc) {
            existing.push_back(file);
        }
    }
    if (existing.empty()) {
        return {};
    }

    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    std::filesystem::path backup;
    for (int attempt = 0; attempt < 100; ++attempt) {
        backup = dir / (".backup." + std::to_string(stamp) + "." +
                        std::to_string(attempt));
        if (std::filesystem::create_directory(backup, ec)) {
            break;
        }
        if (ec != std::errc::file_exists) {
            return {};
        }
        ec.clear();
        backup.clear();
    }
    if (backup.empty()) {
        ec = std::make_error_code(std::errc::file_exists);
        return {};
    }

    for (const auto &file : existing) {
        std::filesystem::copy_file(file, backup / file.filename(),
                                   std::filesystem::copy_options::none, ec);
        if (ec) {
            std::error_code cleanupEc;
            std::filesystem::remove_all(backup, cleanupEc);
            return {};
        }
    }
    return backup;
}

bool ensureDataDirectory() {
    std::error_code ec;
    if (inputer::ensureUserDataDir(ec)) {
        return true;
    }
    std::cerr << "ari-ime-dict: cannot prepare user data directory: "
              << ec.message() << '\n';
    return false;
}

bool openEngine(Zhuyin &engine) {
    if (engine.ok()) {
        return true;
    }
    std::cerr << "ari-ime-dict: libchewing could not be initialized\n";
    return false;
}

int commandInfo() {
    if (!ensureDataDirectory()) {
        return 1;
    }
    Zhuyin engine;
    if (!openEngine(engine)) {
        return 1;
    }
    const auto entries = engine.userPhrases();
    std::cout << "format\tAri IME user dictionary v1\n"
              << "data_dir\t" << inputer::userDataDir().string() << '\n'
              << "libchewing\t" << INPUTER_CHEWING_VERSION << '\n'
              << "entries\t" << entries.size() << '\n';
    return 0;
}

int commandList() {
    if (!ensureDataDirectory()) {
        return 1;
    }
    Zhuyin engine;
    if (!openEngine(engine)) {
        return 1;
    }
    return writeEntries(std::cout, engine.userPhrases()) ? 0 : 1;
}

int commandExport(const std::string &filename) {
    if (!ensureDataDirectory()) {
        return 1;
    }
    Zhuyin engine;
    if (!openEngine(engine)) {
        return 1;
    }

    if (filename == "-" || filename.empty()) {
        return writeEntries(std::cout, engine.userPhrases()) ? 0 : 1;
    }
    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "ari-ime-dict: cannot open export file: " << filename
                  << '\n';
        return 1;
    }
    if (!writeEntries(out, engine.userPhrases())) {
        std::cerr << "ari-ime-dict: cannot write export file: " << filename
                  << '\n';
        return 1;
    }
    return 0;
}

int commandImport(const std::string &filename, bool dryRun) {
    std::ifstream in;
    if (filename != "-") {
        in.open(filename, std::ios::binary);
        if (!in) {
            std::cerr << "ari-ime-dict: cannot open import file: " << filename
                      << '\n';
            return 1;
        }
    }
    std::istream &source = filename == "-" ? std::cin : in;
    std::vector<Entry> entries;
    std::string parseError;
    if (!readEntries(source, entries, parseError)) {
        std::cerr << "ari-ime-dict: invalid import: " << parseError << '\n';
        return 2;
    }
    if (dryRun) {
        std::cout << "validated " << entries.size() << " entr"
                  << (entries.size() == 1 ? "y" : "ies") << "\n";
        return 0;
    }
    if (!ensureDataDirectory()) {
        return 1;
    }
    Zhuyin engine;
    if (!openEngine(engine)) {
        return 1;
    }
    std::unordered_set<std::string> existing;
    for (const auto &entry : engine.userPhrases()) {
        existing.insert(entryKey(entry.phrase, entry.reading));
    }

    std::error_code backupEc;
    const auto backup = makeBackup(backupEc);
    if (backupEc) {
        std::cerr << "ari-ime-dict: cannot create backup before import: "
                  << backupEc.message() << '\n';
        return 1;
    }

    int added = 0;
    int skipped = 0;
    for (const auto &entry : entries) {
        if (!existing.insert(entryKey(entry.phrase, entry.reading)).second) {
            ++skipped;
            continue;
        }
        const int result = engine.addUserPhrase(entry.phrase, entry.reading);
        if (result < 0) {
            std::cerr << "ari-ime-dict: libchewing rejected an entry for "
                      << entry.phrase << '\n';
            if (!backup.empty()) {
                std::cerr << "A backup was kept at " << backup.string() << '\n';
            }
            return 1;
        }
        if (result == 0) {
            ++skipped;
        } else {
            added += result;
        }
    }
    std::cout << "added " << added << ", skipped " << skipped;
    if (!backup.empty()) {
        std::cout << ", backup " << backup.string();
    }
    std::cout << '\n';
    return 0;
}

int commandBackup() {
    if (!ensureDataDirectory()) {
        return 1;
    }
    std::error_code ec;
    const auto backup = makeBackup(ec);
    if (ec) {
        std::cerr << "ari-ime-dict: cannot create backup: " << ec.message()
                  << '\n';
        return 1;
    }
    if (backup.empty()) {
        std::cout << "no learned dictionary files or preference files to back up\n";
    } else {
        std::cout << backup.string() << '\n';
    }
    return 0;
}

int commandCandidates(const std::string &keys) {
    if (!ensureDataDirectory()) {
        return 1;
    }
    Zhuyin engine;
    if (!openEngine(engine)) {
        return 1;
    }
    engine.feedSequence(keys);
    // Match the interactive Buffer's treatment of explicit personal
    // preferences, so this diagnostic reports what Ari will actually show
    // rather than only libchewing's raw cross-version ranking.
    engine.promoteUserPhrases();
    std::cout << "keys\t" << keys << '\n'
              << "preedit\t" << engine.preedit() << '\n';
    if (!engine.openCandidates()) {
        std::cerr << "ari-ime-dict: no candidates for the supplied keys\n";
        return 1;
    }
    std::cout << "total\t" << engine.candidateCount() << '\n'
              << "page\t" << engine.candCurrentPage() + 1 << "/"
              << engine.candTotalPage() << '\n';
    for (const auto &candidate : engine.pageCandidates()) {
        std::cout << "candidate\t" << candidate << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        usage(std::cout);
        return argc < 2 ? 2 : 0;
    }

    const std::string command = argv[1];
    if (command == "info" && argc == 2) {
        return commandInfo();
    }
    if (command == "list" && argc == 2) {
        return commandList();
    }
    if (command == "export" && (argc == 2 || argc == 3)) {
        return commandExport(argc == 3 ? argv[2] : "-");
    }
    if (command == "import" && (argc == 3 || argc == 4)) {
        bool dryRun = false;
        std::string filename;
        if (argc == 3) {
            filename = argv[2];
        } else if (std::string_view(argv[2]) == "--dry-run") {
            dryRun = true;
            filename = argv[3];
        } else {
            usage(std::cerr);
            return 2;
        }
        return commandImport(filename, dryRun);
    }
    if (command == "backup" && argc == 2) {
        return commandBackup();
    }
    if (command == "candidates" && argc == 3) {
        return commandCandidates(argv[2]);
    }

    usage(std::cerr);
    return 2;
}
