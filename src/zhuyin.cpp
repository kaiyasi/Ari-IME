// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "zhuyin.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef ARI_IME_WASM
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include <chewing.h>

#include "constants.h"
#include "layout.h"
#include "unicode.h"

namespace {

constexpr std::string_view kPreferenceHeader =
    "# Ari IME preferred phrases v1";

bool validPreferencePhrase(std::string_view phrase) {
    return !phrase.empty() && phrase.find('\t') == std::string_view::npos &&
           phrase.find('\n') == std::string_view::npos &&
           phrase.find('\r') == std::string_view::npos;
}

using PhraseSet = std::unordered_set<std::string>;

bool readPreferredPhrases(PhraseSet &phrases) {
    const auto path = ari_ime::userPreferencePath();
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return !ec;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (validPreferencePhrase(line)) {
            phrases.insert(line);
        }
    }
    return !in.bad();
}

bool writePreferredPhrases(
    const std::unordered_set<std::string> &phrases) {
    const auto path = ari_ime::userPreferencePath();
    const auto dir = ari_ime::userDataDir();
    if (path.empty() || dir.empty()) {
        return false;
    }

    std::error_code ec;
    if (phrases.empty()) {
        std::filesystem::remove(path, ec);
        return !ec;
    }
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }

    std::vector<std::string> ordered;
    ordered.reserve(phrases.size());
    for (const auto &phrase : phrases) {
        if (validPreferencePhrase(phrase)) {
            ordered.push_back(phrase);
        }
    }
    std::sort(ordered.begin(), ordered.end());

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << kPreferenceHeader << '\n';
        for (const auto &phrase : ordered) {
            out << phrase << '\n';
        }
        if (!out) {
            std::error_code cleanupEc;
            std::filesystem::remove(temporary, cleanupEc);
            return false;
        }
    }

    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code cleanupEc;
        std::filesystem::remove(temporary, cleanupEc);
        return false;
    }
    return true;
}

// A context's cache can be older than another input context or ari-ime-dict.
// Serialize read-modify-write across threads and processes, then reload from
// disk under the lock so one addition cannot erase another.
std::mutex preferenceMutex;

bool updatePreferredPhrases(const std::function<void(PhraseSet &)> &change,
                            PhraseSet &updated) {
    std::lock_guard<std::mutex> guard(preferenceMutex);
    const auto dir = ari_ime::userDataDir();
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }
#ifndef ARI_IME_WASM
    const auto lockPath = dir / "preferences.tsv.lock";
    const int fd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return false;
    }
    int lockResult;
    do {
        lockResult = flock(fd, LOCK_EX);
    } while (lockResult != 0 && errno == EINTR);
    if (lockResult != 0) {
        close(fd);
        return false;
    }
#endif
    PhraseSet current;
    bool ok = readPreferredPhrases(current);
    if (ok) {
        PhraseSet previous = current;
        change(current);
        ok = current == previous || writePreferredPhrases(current);
        if (ok) {
            updated = std::move(current);
        }
    }
#ifndef ARI_IME_WASM
    flock(fd, LOCK_UN);
    close(fd);
#endif
    return ok;
}

// libchewing logs an error for a missing user dictionary even though an empty
// dictionary is the normal first-run state and will be created by autoLearn.
// Ari reports actual context-creation failure through engineReady(), so keep
// the library's low-level logger quiet instead of flooding fcitx's stderr.
void quietChewingLogger(void *, int, const char *, ...) {}

// RAII helper that sets an environment variable for the duration of the scope
// and restores the previous value (or unsets it) afterwards. Used to pin
// libchewing's learned-dictionary location only while we build our context, so
// a sibling libchewing input method in the same process is not redirected.
class ScopedEnv {
public:
    ScopedEnv(const char *name, const std::string &value) : name_(name) {
        if (value.empty()) {
            return;
        }
        if (const char *old = std::getenv(name)) {
            had_ = true;
            old_ = old;
        }
        setenv(name, value.c_str(), 1);
        active_ = true;
    }
    ~ScopedEnv() {
        if (!active_) {
            return;
        }
        if (had_) {
            setenv(name_, old_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

private:
    const char *name_;
    std::string old_;
    bool had_ = false;
    bool active_ = false;
};

} // namespace

Zhuyin::Zhuyin() {
    std::error_code ec;
    const bool haveUserDataDir = ari_ime::ensureUserDataDir(ec);
#ifdef ARI_IME_WASM
    // The portable C libchewing used by the WASM build expects a writable
    // user-dictionary file. Newer native builds use a different explicit path
    // convention, so keep this compatibility detail local to WASM.
    const std::string path =
        haveUserDataDir
            ? (ari_ime::userDataDir() / "uhash.dat").string()
            : std::string{};
#else
    const std::string path =
        haveUserDataDir ? ari_ime::userDictionaryPath().string() : std::string{};
#endif
    // libchewing 0.12 stores its learned user dictionary (chewing.dat /
    // chewing-deleted.dat) at CHEWING_USER_PATH, falling back to
    // $XDG_DATA_HOME/chewing — NOT the `userpath` file below. Left to the
    // default it pollutes the shared chewing data directory used by every
    // libchewing input method. Pin it to Ari's own data directory so learning
    // stays self-contained and resettable; restored right after construction.
    const std::string dataDir =
        haveUserDataDir ? ari_ime::userDataDir().string() : std::string{};
    ScopedEnv chewingUserPath("CHEWING_USER_PATH", dataDir);
#ifdef ARI_IME_WASM
    // The WASM package preloads libchewing's dictionary files into MEMFS at
    // this stable path. Native Fcitx builds continue using libchewing's normal
    // compiled-in search path.
    constexpr const char *kWasmSystemPath = "/usr/share/libchewing";
    const char *systemPath = kWasmSystemPath;
#else
    const char *systemPath = nullptr;
#endif
    ctx_ = chewing_new2(systemPath,
                        path.empty() ? nullptr : path.c_str(),
                        quietChewingLogger, nullptr);
    if (!ctx_ && !path.empty()) {
        // The user-dictionary path was unusable (unwritable directory, corrupt
        // file). Retry against chewing's built-in read-only dictionary so the
        // engine still works this session; we only lose per-user learning.
        ctx_ = chewing_new2(nullptr, nullptr, quietChewingLogger, nullptr);
    }
    if (!ctx_) {
        return;
    }
    chewing_set_KBType(
        ctx_, ari_ime::chewingKeyboardType(ari_ime::currentKeyboardLayout()));
    // Tests run with a throwaway dictionary and should not try to persist learned
    // data; production keeps auto-learning enabled by default.
    setLearningAllowed(true);
#if defined(CHEWING_VERSION_MAJOR) && defined(CHEWING_VERSION_MINOR) &&           \
    (CHEWING_VERSION_MAJOR > 0 || CHEWING_VERSION_MINOR >= 9)
    // Newer libchewing versions can rank candidates by learned frequency. Query
    // the option because it is not present in every build with the config API.
    constexpr const char *kSortByFrequency =
        "chewing.sort_candidates_by_frequency";
    if (chewing_config_has_option(ctx_, kSortByFrequency) == 1) {
        chewing_config_set_int(ctx_, kSortByFrequency, 1);
    }
#endif
    chewing_set_spaceAsSelection(ctx_, 0);    // We drive selection ourselves.
    chewing_set_escCleanAllBuf(ctx_, 1);
    chewing_set_candPerPage(ctx_, ari_ime::kCandPerPage);
    chewing_set_maxChiSymbolLen(ctx_, ari_ime::kMaxCompositionChars);
    // Load personal mappings before the context starts receiving user input.
    // Older libchewing releases can disturb a live long pre-edit when their
    // userphrase enumeration API is called mid-composition.
    loadUserPhraseCache();
}

Zhuyin::~Zhuyin() {
    if (ctx_) {
        chewing_delete(ctx_);
        ctx_ = nullptr;
    }
}

void Zhuyin::setLearningAllowed(bool allowed) {
    learningAllowed_ = allowed && ari_ime::autoLearnEnabled();
    if (ctx_) {
        chewing_set_autoLearn(ctx_, learningAllowed_ ? AUTOLEARN_ENABLED
                                                    : AUTOLEARN_DISABLED);
    }
}

void Zhuyin::resetAll() {
    if (ctx_) {
        chewing_Reset(ctx_);
#ifdef ARI_IME_LIBCHEWING_LEGACY_OUTPUT
        // libchewing 0.6 resets the editor state but leaves its compatibility
        // display buffer populated until Esc is handled explicitly.
        chewing_handle_Esc(ctx_);
#endif
    }
}

void Zhuyin::setKeyboardLayout(ari_ime::KeyboardLayout layout) {
    if (layout_ != layout) {
        reverseReadings_.clear();
        reverseReadingsOrder_.clear();
    }
    layout_ = layout;
    if (ctx_) {
        chewing_set_KBType(ctx_, ari_ime::chewingKeyboardType(layout));
    }
}

std::vector<std::string> Zhuyin::readingsForText(const std::string &text) {
    const auto chars = ari_ime::unicode::splitGraphemes(text);
    std::vector<std::string> readings(chars.size());
    if (!ctx_ || chars.empty()) {
        return readings;
    }

    std::unordered_map<std::string, std::vector<std::size_t>> wanted;
    wanted.reserve(chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        wanted[chars[i]].push_back(i);
    }

    std::size_t remaining = chars.size();
    for (const auto &[character, indices] : wanted) {
        const auto cached = reverseReadings_.find(character);
        if (cached == reverseReadings_.end()) {
            continue;
        }
        for (const std::size_t index : indices) {
            readings[index] = cached->second;
            --remaining;
        }
    }
    if (remaining == 0) {
        return readings;
    }

    Zhuyin probe;
    probe.setKeyboardLayout(layout_);
    for (const auto &sequence : ari_ime::syllableKeySequences(layout_)) {
        if (remaining == 0) {
            break;
        }
        probe.resetAll();
        for (char key : sequence.keys) {
            probe.handleDefault(static_cast<int>(key));
        }
        if (sequence.toneOne) {
            probe.handleSpace();
        }
        if (!probe.openCandidates()) {
            continue;
        }
        const int total = probe.candidateCount();
        for (int i = 0; i < total; ++i) {
            const std::string candidate = probe.candidate(i);
            if (ari_ime::unicode::graphemeCount(candidate) != 1) {
                continue;
            }
            const auto wantedIt = wanted.find(candidate);
            if (wantedIt == wanted.end()) {
                continue;
            }
            std::string reading = sequence.keys;
            if (sequence.toneOne) {
                reading.push_back(' ');
            }
            auto [entry, inserted] =
                reverseReadings_.try_emplace(candidate, reading);
            if (inserted) {
                reverseReadingsOrder_.push_back(entry->first);
            }
            for (const std::size_t index : wantedIt->second) {
                if (!readings[index].empty()) {
                    continue;
                }
                readings[index] = reading;
                --remaining;
            }
        }
        probe.closeCandidates();
    }
    constexpr std::size_t kMaxReverseReadings = 4096;
    while (reverseReadings_.size() > kMaxReverseReadings &&
           !reverseReadingsOrder_.empty()) {
        reverseReadings_.erase(reverseReadingsOrder_.front());
        reverseReadingsOrder_.pop_front();
    }
    probe.resetAll();
    return readings;
}

void Zhuyin::feedKey(char c) {
    if (ctx_) {
        chewing_handle_Default(ctx_, static_cast<int>(c));
    }
}

void Zhuyin::feedSequence(const std::string &keys) {
    if (!ctx_) {
        return;
    }
    resetAll();
    for (char c : keys) {
        chewing_handle_Default(ctx_, static_cast<int>(c));
    }
}

bool Zhuyin::hasConverted() const {
    return ctx_ && chewing_buffer_Check(ctx_) == 1;
}

bool Zhuyin::hasBopomofo() const {
    return ctx_ && chewing_bopomofo_Check(ctx_) == 1;
}

std::string Zhuyin::preedit() const {
    if (!ctx_) {
        return {};
    }
    std::string out;
    if (chewing_buffer_Check(ctx_) == 1) {
        if (const char *s = chewing_buffer_String_static(ctx_)) {
            out += s;
        }
    }
    if (chewing_bopomofo_Check(ctx_) == 1) {
        if (const char *s = chewing_bopomofo_String_static(ctx_)) {
            out += s;
        }
    }
    return out;
}

std::string Zhuyin::bopomofoString() const {
    if (!ctx_ || chewing_bopomofo_Check(ctx_) != 1) {
        return {};
    }
    if (const char *s = chewing_bopomofo_String_static(ctx_)) {
        return s;
    }
    return {};
}

void Zhuyin::handleDefault(int key) {
    if (ctx_) {
        chewing_handle_Default(ctx_, key);
    }
}
void Zhuyin::handleSpace() {
    if (ctx_) {
        chewing_handle_Space(ctx_);
    }
}
void Zhuyin::handleEnter() {
    if (ctx_) {
        chewing_handle_Enter(ctx_);
    }
}
void Zhuyin::handleEsc() {
    if (ctx_) {
        chewing_handle_Esc(ctx_);
    }
}
void Zhuyin::handleBackspace() {
    if (ctx_) {
        chewing_handle_Backspace(ctx_);
    }
}
void Zhuyin::handleDelete() {
    if (ctx_) {
        chewing_handle_Del(ctx_);
    }
}
void Zhuyin::handleUp() {
    if (ctx_) {
        chewing_handle_Up(ctx_);
    }
}
void Zhuyin::handleDown() {
    if (ctx_) {
        chewing_handle_Down(ctx_);
    }
}
void Zhuyin::handleLeft() {
    if (ctx_) {
        chewing_handle_Left(ctx_);
    }
}
void Zhuyin::handleRight() {
    if (ctx_) {
        chewing_handle_Right(ctx_);
    }
}
void Zhuyin::handleHome() {
    if (ctx_) {
        chewing_handle_Home(ctx_);
    }
}
void Zhuyin::handleEnd() {
    if (ctx_) {
        chewing_handle_End(ctx_);
    }
}

int Zhuyin::forgetUserPhrase(const std::string &phrase) {
    if (!ctx_ || phrase.empty()) {
        return -1;
    }
    loadUserPhraseCache();
    if (chewing_userphrase_enumerate(ctx_) != 0) {
        return -1;
    }

    std::vector<std::string> readings;
    unsigned int phraseLen = 0;
    unsigned int bopomofoLen = 0;
    while (chewing_userphrase_has_next(ctx_, &phraseLen, &bopomofoLen) == 1) {
        if (phraseLen == 0 || bopomofoLen == 0) {
            continue;
        }
        std::vector<char> phraseBuf(phraseLen);
        std::vector<char> bopomofoBuf(bopomofoLen);
        if (chewing_userphrase_get(ctx_, phraseBuf.data(), phraseLen,
                                   bopomofoBuf.data(), bopomofoLen) == 0 &&
            phrase == phraseBuf.data()) {
            readings.emplace_back(bopomofoBuf.data());
        }
    }

    int removed = 0;
    for (const auto &reading : readings) {
        const int count =
            chewing_userphrase_remove(ctx_, phrase.c_str(), reading.c_str());
        if (count < 0) {
            return -1;
        }
        removed += count;
    }
    bool preferred = false;
    std::unordered_set<std::string> updated;
    if (!updatePreferredPhrases(
            [&](auto &phrases) { preferred = phrases.erase(phrase) > 0; },
            updated)) {
        return -1;
    }
    userPhraseTexts_ = std::move(updated);
    userPhraseCacheLoaded_ = true;
    userPhraseMappings_.erase(phrase);
    // The Ari sidecar is itself a personal preference. Count its removal even
    // when an older libchewing dictionary has no matching reading to remove.
    return removed > 0 ? removed : (preferred ? 1 : 0);
}

std::vector<UserPhrase> Zhuyin::userPhrases() {
    std::vector<UserPhrase> out;
    if (!ctx_ || chewing_userphrase_enumerate(ctx_) != 0) {
        return out;
    }

    unsigned int phraseLen = 0;
    unsigned int bopomofoLen = 0;
    while (chewing_userphrase_has_next(ctx_, &phraseLen, &bopomofoLen) == 1) {
        if (phraseLen == 0 || bopomofoLen == 0) {
            continue;
        }
        std::vector<char> phraseBuf(phraseLen);
        std::vector<char> bopomofoBuf(bopomofoLen);
        if (chewing_userphrase_get(ctx_, phraseBuf.data(), phraseLen,
                                   bopomofoBuf.data(), bopomofoLen) == 0) {
            out.push_back({phraseBuf.data(), bopomofoBuf.data()});
        }
    }
    return out;
}

int Zhuyin::addUserPhrase(const std::string &phrase,
                          const std::string &reading) {
    if (!ctx_ || phrase.empty() || reading.empty()) {
        return -1;
    }
    const int result =
        chewing_userphrase_add(ctx_, phrase.c_str(), reading.c_str());
    const bool exists =
        chewing_userphrase_lookup(ctx_, phrase.c_str(), reading.c_str()) == 1;
    if (result > 0 || exists) {
        userPhraseCacheLoaded_ = true;
        // This sidecar records deliberate selected/imported preferences only
        // for Ari's portable bookkeeping. The libchewing user dictionary also
        // contains ordinary learned frequencies and remains the live scorer.
        std::unordered_set<std::string> updated;
        if (updatePreferredPhrases(
                [&](auto &phrases) { phrases.insert(phrase); }, updated)) {
            userPhraseTexts_ = std::move(updated);
            userPhraseMappings_.insert(phrase);
        }
    }
    return result;
}

bool Zhuyin::rememberPreferredPhrase(const std::string &phrase) {
    if (!ctx_ || !learningAllowed_ ||
        !validPreferencePhrase(phrase)) {
        return false;
    }
    std::unordered_set<std::string> updated;
    if (!updatePreferredPhrases(
            [&](auto &phrases) { phrases.insert(phrase); }, updated)) {
        return false;
    }
    userPhraseTexts_ = std::move(updated);
    userPhraseCacheLoaded_ = true;
    userPhraseMappings_.insert(phrase);
    return true;
}

bool Zhuyin::loadUserPhraseCache() {
    if (userPhraseCacheLoaded_) {
        return !userPhraseTexts_.empty();
    }
    userPhraseCacheLoaded_ = true;
    // Do not enumerate libchewing's broad learned dictionary here. Older C
    // APIs expose ordinary frequency learning and explicit user phrases as the
    // same entries; importing that broad set is unnecessary and can disturb a
    // long active window. Ari's own sidecar is deliberately unambiguous.
    readPreferredPhrases(userPhraseTexts_);
#ifdef ARI_IME_LEGACY_PREFERENCE_PROMOTION
    // The sidecar records which phrases were deliberately chosen, while the
    // libchewing dictionary confirms that the corresponding mapping actually
    // exists. Intersecting both stores avoids promoting stale sidecar text.
    if (!userPhraseTexts_.empty()) {
        for (const auto &entry : userPhrases()) {
            if (userPhraseTexts_.find(entry.phrase) != userPhraseTexts_.end()) {
                userPhraseMappings_.insert(entry.phrase);
            }
        }
    }
#endif
    return !userPhraseTexts_.empty();
}

int Zhuyin::promoteUserPhrases() {
#ifndef ARI_IME_LEGACY_PREFERENCE_PROMOTION
    return 0;
#else
    if (!ctx_ || !loadUserPhraseCache()) {
        return 0;
    }

    struct Choice {
        int start = 0;
        int down = 0;
        int index = 0;
        int length = 0;
    };

    int applied = 0;
    for (int pass = 0; pass < ari_ime::kMaxCompositionChars; ++pass) {
        const auto visible = ari_ime::unicode::splitGraphemes(preedit());
        if (visible.empty()) {
            break;
        }

        Choice best;
        bool found = false;
        for (int start = 0; start < static_cast<int>(visible.size()); ++start) {
            closeCandidates();
            handleHome();
            for (int i = 0; i < start; ++i) {
                handleRight();
            }
            if (!openCandidates()) {
                continue;
            }

            for (int down = 0, guard = 0;
                 guard < ari_ime::kMaxSyllables; ++guard, ++down) {
                const int total = candidateCount();
                for (int index = 0; index < total; ++index) {
                    const std::string text = candidate(index);
                    if (userPhraseTexts_.find(text) == userPhraseTexts_.end() ||
                        userPhraseMappings_.find(text) ==
                            userPhraseMappings_.end()) {
                        continue;
                    }
                    const auto candidateChars =
                        ari_ime::unicode::splitGraphemes(text);
                    const int length = static_cast<int>(candidateChars.size());
                    if (length <= 0 ||
                        start + length > static_cast<int>(visible.size())) {
                        continue;
                    }
                    bool alreadyVisible = true;
                    for (int i = 0; i < length; ++i) {
                        if (visible[start + i] != candidateChars[i]) {
                            alreadyVisible = false;
                            break;
                        }
                    }
                    if (alreadyVisible) {
                        continue;
                    }

                    // Prefer the longest explicit phrase. For equal lengths,
                    // prefer the rightmost span so a second phrase in a
                    // sentence is not hidden by an already-correct first one.
                    if (!found || length > best.length ||
                        (length == best.length && start > best.start) ||
                        (length == best.length && start == best.start &&
                         down < best.down)) {
                        best = {start, down, index, length};
                        found = true;
                    }
                }

                if (total <= 0 ||
                    ari_ime::unicode::graphemeCount(candidate(0)) <= 1) {
                    break;
                }
                handleDown();
            }
        }

        if (!found) {
            closeCandidates();
            handleEnd();
            break;
        }

        closeCandidates();
        handleHome();
        for (int i = 0; i < best.start; ++i) {
            handleRight();
        }
        if (!openCandidates()) {
            break;
        }
        for (int i = 0; i < best.down; ++i) {
            handleDown();
        }
        // chewing_cand_choose_by_index expects a global index; do not page and
        // pass best.index % perPage (that remaps onto page-1 slots).
        chooseCandidate(best.index);
        ++applied;
    }

    closeCandidates();
    handleEnd();
    return applied;
#endif
}

std::vector<std::pair<int, int>> Zhuyin::phraseIntervals() {
    std::vector<std::pair<int, int>> out;
    if (!ctx_) {
        return out;
    }
    chewing_interval_Enumerate(ctx_);
    while (chewing_interval_hasNext(ctx_) == 1) {
        IntervalType interval{};
        chewing_interval_Get(ctx_, &interval);
        if (interval.from >= 0 && interval.to > interval.from) {
            out.emplace_back(interval.from, interval.to);
        }
    }
    return out;
}

bool Zhuyin::absorbed() const {
    return ctx_ && chewing_keystroke_CheckAbsorb(ctx_) == 1;
}

bool Zhuyin::ignored() const {
    return ctx_ && chewing_keystroke_CheckIgnore(ctx_) == 1;
}

std::vector<std::string> Zhuyin::pageCandidates() const {
    std::vector<std::string> out;
    if (!ctx_) {
        return out;
    }
    int total = chewing_cand_TotalChoice(ctx_);
    if (total <= 0) {
        return out;
    }
    int perPage = chewing_cand_ChoicePerPage(ctx_);
    if (perPage <= 0) {
        perPage = 9;
    }
    int page = chewing_cand_CurrentPage(ctx_);
    int start = page * perPage;
    for (int i = start; i < start + perPage && i < total; ++i) {
        if (const char *s = chewing_cand_string_by_index_static(ctx_, i)) {
            out.emplace_back(s);
        }
    }
    return out;
}

int Zhuyin::cursorPos() const {
    return ctx_ ? chewing_cursor_Current(ctx_) : 0;
}

int Zhuyin::candPerPage() const {
    if (!ctx_) {
        return 9;
    }
    int n = chewing_cand_ChoicePerPage(ctx_);
    return n > 0 ? n : 9;
}

int Zhuyin::candCurrentPage() const {
    return ctx_ ? chewing_cand_CurrentPage(ctx_) : 0;
}

int Zhuyin::candTotalPage() const {
    return ctx_ ? chewing_cand_TotalPage(ctx_) : 0;
}

void Zhuyin::nextPage() {
    if (ctx_) {
        chewing_handle_PageDown(ctx_);
    }
}

void Zhuyin::prevPage() {
    if (ctx_) {
        chewing_handle_PageUp(ctx_);
    }
}

bool Zhuyin::openCandidates() {
    return ctx_ && chewing_cand_open(ctx_) == 0;
}

void Zhuyin::closeCandidates() {
    if (ctx_) {
        chewing_cand_close(ctx_);
    }
}

int Zhuyin::candidateCount() const {
    return ctx_ ? chewing_cand_TotalChoice(ctx_) : 0;
}

std::string Zhuyin::candidate(int index) const {
    if (!ctx_) {
        return {};
    }
    if (const char *s = chewing_cand_string_by_index_static(ctx_, index)) {
        return s;
    }
    return {};
}

void Zhuyin::chooseCandidate(int index) {
    if (ctx_) {
        chewing_cand_choose_by_index(ctx_, index);
    }
}

void Zhuyin::cleanBopomofo() {
    if (ctx_) {
        chewing_clean_bopomofo_buf(ctx_);
    }
}

void Zhuyin::forceCommitPreedit() {
    if (ctx_) {
        chewing_commit_preedit_buf(ctx_);
    }
}

bool Zhuyin::hasCommit() const {
    return ctx_ && chewing_commit_Check(ctx_) == 1;
}

std::string Zhuyin::takeCommit() {
    if (!ctx_) {
        return {};
    }
    std::string out;
    if (chewing_commit_Check(ctx_) == 1) {
        if (const char *s = chewing_commit_String_static(ctx_)) {
            out = s;
        }
    }
#ifdef ARI_IME_LIBCHEWING_LEGACY_OUTPUT
    // libchewing 0.6 has no chewing_ack(). Processing an ignored key performs
    // the same output-buffer rollover without changing the composition.
    chewing_handle_Default(ctx_, 0);
#else
    chewing_ack(ctx_);
#endif
    return out;
}
