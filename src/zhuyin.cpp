// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "zhuyin.h"

#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

#include <chewing.h>

#include "constants.h"
#include "layout.h"
#include "unicode.h"

namespace {

bool hasUserPhraseStore() {
    const auto dir = inputer::userDataDir();
    if (dir.empty()) {
        return false;
    }

    std::error_code ec;
    if (std::filesystem::is_regular_file(dir / "userdict.dat", ec) &&
        !ec) {
        return true;
    }
    ec.clear();
    return std::filesystem::is_regular_file(dir / "chewing.dat", ec) &&
           !ec;
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
    const bool haveUserDataDir = inputer::ensureUserDataDir(ec);
    const std::string path =
        haveUserDataDir ? inputer::userDictionaryPath().string() : std::string{};
    // libchewing 0.12 stores its learned user dictionary (chewing.dat /
    // chewing-deleted.dat) at CHEWING_USER_PATH, falling back to
    // $XDG_DATA_HOME/chewing — NOT the `userpath` file below. Left to the
    // default it pollutes the shared chewing data directory used by every
    // libchewing input method. Pin it to Ari's own data directory so learning
    // stays self-contained and resettable; restored right after construction.
    const std::string dataDir =
        haveUserDataDir ? inputer::userDataDir().string() : std::string{};
    ScopedEnv chewingUserPath("CHEWING_USER_PATH", dataDir);
    ctx_ = chewing_new2(nullptr, path.empty() ? nullptr : path.c_str(),
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
        ctx_, inputer::chewingKeyboardType(inputer::currentKeyboardLayout()));
    // Tests run with a throwaway dictionary and should not try to persist learned
    // data; production keeps auto-learning enabled by default.
    chewing_set_autoLearn(
        ctx_, inputer::autoLearnEnabled() ? AUTOLEARN_ENABLED
                                          : AUTOLEARN_DISABLED);
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
    chewing_set_candPerPage(ctx_, inputer::kCandPerPage);
    chewing_set_maxChiSymbolLen(ctx_, inputer::kMaxCompositionChars);
}

Zhuyin::~Zhuyin() {
    if (ctx_) {
        chewing_delete(ctx_);
        ctx_ = nullptr;
    }
}

void Zhuyin::resetAll() {
    if (ctx_) {
        chewing_Reset(ctx_);
#ifdef INPUTER_LIBCHEWING_LEGACY_OUTPUT
        // libchewing 0.6 resets the editor state but leaves its compatibility
        // display buffer populated until Esc is handled explicitly.
        chewing_handle_Esc(ctx_);
#endif
    }
}

void Zhuyin::setKeyboardLayout(inputer::KeyboardLayout layout) {
    if (ctx_) {
        chewing_set_KBType(ctx_, inputer::chewingKeyboardType(layout));
    }
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
    if (!ctx_ || phrase.empty() || chewing_userphrase_enumerate(ctx_) != 0) {
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
    if (userPhraseCacheLoaded_) {
        userPhraseTexts_.erase(phrase);
    }
    return removed;
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
    if (result >= 0) {
        userPhraseCacheLoaded_ = true;
        userPhraseTexts_.insert(phrase);
    }
    return result;
}

bool Zhuyin::loadUserPhraseCache() {
    if (userPhraseCacheLoaded_) {
        return !userPhraseTexts_.empty();
    }
    // A fresh Ari profile normally has only the directory, not a dictionary
    // file. Avoid calling the user-phrase enumeration API in that state: it is
    // needlessly expensive and older libchewing releases can perturb a long
    // candidate window even when the dictionary is empty. Leave the cache
    // unopened so a file created later by learning is picked up next time.
    if (!hasUserPhraseStore()) {
        return false;
    }

    userPhraseCacheLoaded_ = true;
    for (const auto &entry : userPhrases()) {
        if (!entry.phrase.empty()) {
            userPhraseTexts_.insert(entry.phrase);
        }
    }
    return !userPhraseTexts_.empty();
}

int Zhuyin::promoteUserPhrases() {
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
    for (int pass = 0; pass < inputer::kMaxCompositionChars; ++pass) {
        const auto visible = inputer::unicode::splitGraphemes(preedit());
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
                 guard < inputer::kMaxSyllables; ++guard, ++down) {
                const int total = candidateCount();
                for (int index = 0; index < total; ++index) {
                    const std::string text = candidate(index);
                    if (userPhraseTexts_.find(text) == userPhraseTexts_.end()) {
                        continue;
                    }
                    const auto candidateChars =
                        inputer::unicode::splitGraphemes(text);
                    const int length = static_cast<int>(candidateChars.size());
                    if (length <= 0 || start + length >
                                           static_cast<int>(visible.size())) {
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
                    // prefer the newest/rightmost span so a second phrase in a
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
                    inputer::unicode::graphemeCount(candidate(0)) <= 1) {
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
        const int perPage = candPerPage();
        const int targetPage = perPage > 0 ? best.index / perPage : 0;
        while (candCurrentPage() < targetPage) {
            nextPage();
        }
        chooseCandidate(perPage > 0 ? best.index % perPage : best.index);
        ++applied;
    }

    closeCandidates();
    handleEnd();
    return applied;
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
#ifdef INPUTER_LIBCHEWING_LEGACY_OUTPUT
    // libchewing 0.6 has no chewing_ack(). Processing an ignored key performs
    // the same output-buffer rollover without changing the composition.
    chewing_handle_Default(ctx_, 0);
#else
    chewing_ack(ctx_);
#endif
    return out;
}
