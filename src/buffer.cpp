// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "buffer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

#include <fcitx-utils/keysym.h>

#include "constants.h"

namespace {

// Numeric-keypad keys (NumLock on) arrive as KP_* keysyms instead of the ASCII
// sym of the equivalent main-row key. Map them back to ASCII so they flow
// through the same handleChar path; otherwise they bypass the engine and the
// application inserts them directly, scrambling order against text still held
// in our pre-edit (e.g. typing 2~3 on the keypad produced 23~).
// Number of Unicode characters in a UTF-8 string (counts lead bytes).
int utf8Count(const std::string &s) {
    int n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) {
            ++n;
        }
    }
    return n;
}

// Split a UTF-8 string into its individual characters (codepoints).
std::vector<std::string> splitUtf8(const std::string &s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = (c & 0x80) == 0x00   ? 1
                          : (c & 0xE0) == 0xC0 ? 2
                          : (c & 0xF0) == 0xE0 ? 3
                                               : 4;
        len = std::min(len, s.size() - i);
        out.push_back(s.substr(i, len));
        i += len;
    }
    return out;
}

int keypadAscii(fcitx::KeySym sym) {
    switch (sym) {
    case FcitxKey_KP_0: return '0';
    case FcitxKey_KP_1: return '1';
    case FcitxKey_KP_2: return '2';
    case FcitxKey_KP_3: return '3';
    case FcitxKey_KP_4: return '4';
    case FcitxKey_KP_5: return '5';
    case FcitxKey_KP_6: return '6';
    case FcitxKey_KP_7: return '7';
    case FcitxKey_KP_8: return '8';
    case FcitxKey_KP_9: return '9';
    case FcitxKey_KP_Decimal: return '.';
    case FcitxKey_KP_Divide: return '/';
    case FcitxKey_KP_Multiply: return '*';
    case FcitxKey_KP_Subtract: return '-';
    case FcitxKey_KP_Add: return '+';
    case FcitxKey_KP_Equal: return '=';
    default: return 0;
    }
}

bool hasWordModifier(const fcitx::Key &key) {
    return key.states().testAny(fcitx::KeyStates{
        fcitx::KeyState::Ctrl, fcitx::KeyState::Alt, fcitx::KeyState::Super});
}

// 大千 / KB_DEFAULT key classes. A 注音 syllable is at most one key from each
// class, in the canonical order 聲母 < 介音 < 韻母 < 聲調. We use the class as a
// "slot" so a raw key string can be re-sorted into that order before feeding
// chewing, which makes detection tolerant of out-of-order typing (su3 vs s3u).
//   聲母 = 0, 介音 = 1, 韻母 = 2, 聲調 = 3.  Non-注音 keys return -1.
int slotOf(char c) {
    // O(1) lookup: ASCII key -> 注音 slot (聲母 0, 介音 1, 韻母 2, 聲調 3), -1 otherwise.
    static const std::array<int8_t, 128> kSlot = [] {
        std::array<int8_t, 128> t{};
        t.fill(-1);
        auto mark = [&](std::string_view keys, int8_t slot) {
            for (char k : keys) {
                t[static_cast<unsigned char>(k)] = slot;
            }
        };
        mark("1qaz2wsxedcrfv5tgbyhn", 0); // 聲母
        mark("ujm", 1);                    // 介音 ㄧㄨㄩ
        mark("8ik,9ol.0p;/-", 2);          // 韻母 ㄚ..ㄦ
        mark("3467", 3);                   // 聲調 ˇˋˊ˙
        return t;
    }();
    // Bopomofo has no case: a 注音 key is the same key whether Shift / Caps Lock
    // is on or not, so fold uppercase letters to lowercase before the lookup.
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') {
        uc += 'a' - 'A';
    }
    return uc < kSlot.size() ? kSlot[uc] : -1;
}

bool isToneChar(char c) { return slotOf(c) == 3; }

// Sort raw keys into canonical 注音 order. Each class appears at most once, so a
// stable sort by slot yields the order chewing expects.
std::string canonical(const std::string &keys) {
    std::string out = keys;
    // Fold to lowercase: these keys feed chewing (and become cell readings),
    // which only understands the lowercase 大千 layout. Case is preserved
    // separately on the English fallback path (syl_ / englishBuf_ keep the
    // original characters), so uppercase still types English when it must.
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](char a, char b) { return slotOf(a) < slotOf(b); });
    return out;
}

// True when `keys` are all 注音 keys with no class used twice. When `allowTone`
// is false a 聲調 key disqualifies the string (used to find the bopomofo body of
// a trailing syllable, before the tone is appended).
bool validSyllable(const std::string &keys, bool allowTone) {
    std::array<bool, 4> seen{false, false, false, false};
    for (char c : keys) {
        int s = slotOf(c);
        if (s < 0) {
            return false;
        }
        if (s == 3 && !allowTone) {
            return false;
        }
        if (seen[s]) {
            return false;
        }
        seen[s] = true;
    }
    return true;
}

// Whether a complete (toned) canonical syllable converts to a Chinese character
// with nothing left dangling.
bool syllableConverts(const std::string &canonicalKeys) {
    // Intentionally leaked: a chewing context must outlive the process rather
    // than be torn down during static destruction.
    static Zhuyin *probe = new Zhuyin();
    probe->feedSequence(canonicalKeys);
    return probe->hasConverted() && !probe->hasBopomofo();
}

// Whether a bopomofo body (no tone) yields a character under 一聲 (the tone the
// space key applies). Used to decide if space should convert a pending syllable.
bool syllableConvertsTone1(const std::string &canonicalBody) {
    static Zhuyin *probe = new Zhuyin();
    probe->feedSequence(canonicalBody);
    probe->handleSpace(); // 一聲
    return probe->hasConverted() && !probe->hasBopomofo();
}

// chewing natively maps the shifted / standalone punctuation keys to full-width
// Chinese punctuation (e.g. '<' -> ，, '>' -> 。, '?' -> ？, '[' -> 「). Return that
// mapping for key `c`, or empty for letters/digits/unmapped keys (so they keep the
// literal English path). The 注音 韻母 keys ', . ; / -' never reach here — they form
// bopomofo (ㄝㄡㄤㄥㄦ) and must not be repurposed.
std::string chinesePunct(char c) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
        return {};
    }
    // Intentionally leaked, like the syllable probes above.
    static Zhuyin *probe = new Zhuyin();
    probe->resetAll();
    probe->handleDefault(static_cast<int>(c));
    std::string out;
    if (probe->hasCommit()) {
        out = probe->takeCommit();
    } else {
        probe->forceCommitPreedit();
        out = probe->takeCommit();
    }
    probe->resetAll();
    // Accept only genuine full-width punctuation (non-ASCII); reject ASCII
    // passthrough or empty results.
    if (out.empty() || static_cast<unsigned char>(out[0]) < 0x80) {
        return {};
    }
    return out;
}

// A Chinese cell's reading stores its canonical 注音 keys, with a trailing ' '
// marking a 一聲 syllable (which has no tone key). Split it back into the raw key
// body and whether 一聲 (chewing's space) must be applied after feeding it.
std::pair<std::string, bool> readingBody(const std::string &reading) {
    if (!reading.empty() && reading.back() == ' ') {
        return {reading.substr(0, reading.size() - 1), true};
    }
    return {reading, false};
}

} // namespace

void Buffer::reset() {
    forcedEnglish_ = false;
    token_ = Token::Chinese;
    cells_.clear();
    tail_.clear();
    runReadings_.clear();
    englishBuf_.clear();
    syl_.clear();
    selecting_ = false;
    candOpen_ = false;
    caretPos_ = 0;
    selCursor_ = 0;
    highlight_ = 0;
    selCands_.clear();
    selPage_ = 0;
    runLoaded_ = false;
    zhuyin_.resetAll();
}

// ---------------------------------------------------------------------------
// Freezing the live tail into cells_
// ---------------------------------------------------------------------------

void Buffer::moveAutoCommit() {
    if (!zhuyin_.hasCommit()) {
        return;
    }
    // chewing auto-commits the oldest characters once the run gets long; they
    // belong at the front of the run, i.e. appended to cells_ in order.
    auto chars = splitUtf8(zhuyin_.takeCommit());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        std::string reading = i < runReadings_.size() ? runReadings_[i] : "";
        cells_.push_back({true, chars[i], reading});
    }
    std::size_t n = std::min(chars.size(), runReadings_.size());
    runReadings_.erase(runReadings_.begin(), runReadings_.begin() + n);
}

void Buffer::freezeRun() {
    auto chars = splitUtf8(zhuyin_.preedit());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        std::string reading = i < runReadings_.size() ? runReadings_[i] : "";
        cells_.push_back({true, chars[i], reading});
    }
    runReadings_.clear();
    zhuyin_.resetAll();
}

void Buffer::freezeEnglish() {
    for (char c : englishBuf_) {
        cells_.push_back({false, std::string(1, c), {}});
    }
    englishBuf_.clear();
}

void Buffer::freezeSyllable() {
    for (char c : syl_) {
        cells_.push_back({false, std::string(1, c), {}});
    }
    syl_.clear();
}

void Buffer::freezeAll() {
    freezeRun();
    freezeEnglish();
    freezeSyllable();
    token_ = Token::Chinese;
}

// ---------------------------------------------------------------------------
// Display accessors
// ---------------------------------------------------------------------------

std::string Buffer::preeditText() const {
    std::string out;
    for (const Cell &c : cells_) {
        out += c.text;
    }
    // While selecting, zhuyin_ holds a scratch run for the current cell's
    // candidates (and the tail is already frozen into cells_), so the cells
    // alone are the full pre-edit. Otherwise append the live typing tail.
    if (!selecting_) {
        out += zhuyin_.preedit();
        out += englishBuf_;
        out += syl_;
    }
    // Anything parked while inserting mid-string trails the live tail.
    for (const Cell &c : tail_) {
        out += c.text;
    }
    return out;
}

std::vector<std::string> Buffer::candidates() const {
    // The current page (9) of the merged phrase+single candidate list. Empty on
    // an English cell (cursor still visible, just nothing to pick).
    std::vector<std::string> out;
    if (!selecting_ || !candOpen_) {
        return out; // caret mode shows no candidate window
    }
    int start = selPage_ * inputer::kCandPerPage;
    for (int i = start;
         i < start + inputer::kCandPerPage &&
         i < static_cast<int>(selCands_.size());
         ++i) {
        out.push_back(selCands_[i].text);
    }
    return out;
}

int Buffer::highlight() const {
    return candidates().empty() ? -1 : highlight_;
}

int Buffer::selectionChar() const {
    if (!selecting_ || !candOpen_ || selCursor_ < 0 ||
        selCursor_ >= static_cast<int>(cells_.size())) {
        return -1; // only the picking-mode window highlights a single cell
    }
    int idx = 0;
    for (int i = 0; i < selCursor_; ++i) {
        idx += utf8Count(cells_[i].text);
    }
    return idx;
}

int Buffer::caretChar() const {
    // Where the pre-edit caret should sit (character index), or -1 for "at the
    // very end". While selecting, park it on the cell being edited; while
    // inserting mid-string, park it at the insertion point — right after the
    // head cells and the live typing tail, i.e. just before the parked tail_.
    if (selecting_) {
        // Picking mode parks on the cell being re-picked; caret mode shows the
        // bare caret between cells (one codepoint per cell, so index == caretPos_).
        return candOpen_ ? selectionChar() : caretPos_;
    }
    if (tail_.empty()) {
        return -1;
    }
    int before = static_cast<int>(cells_.size()) // one codepoint per cell
                 + utf8Count(zhuyin_.preedit())
                 + static_cast<int>(englishBuf_.size())
                 + static_cast<int>(syl_.size());
    return before;
}

// ---------------------------------------------------------------------------
// Top-level dispatch
// ---------------------------------------------------------------------------

KeyResult Buffer::handleKey(const fcitx::Key &key) {
    // Ctrl+Space toggles forced pure-English mode (no 注音 interpretation). Like
    // every other transition it folds the live tail into cells_ but never
    // commits; the pre-edit is still only flushed on Enter.
    if (key.check(FcitxKey_space, fcitx::KeyState::Ctrl)) {
        if (selecting_) {
            exitSelection();
        }
        freezeAll();
        forcedEnglish_ = !forcedEnglish_;
        return {true, false, {}, true, /*notifyMode=*/true};
    }

    return handleAuto(key);
}

KeyResult Buffer::handleAuto(const fcitx::Key &key) {
    if (hasWordModifier(key)) {
        return {false, false, {}, false};
    }

    if (selecting_) {
        return handleSelecting(key);
    }

    auto sym = key.sym();

    if (sym == FcitxKey_space) {
        return handleSpace();
    }
    if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
        return handleEnter();
    }
    if (sym == FcitxKey_BackSpace) {
        return handleBackspace();
    }
    if (sym == FcitxKey_Escape) {
        if (!preeditText().empty()) {
            reset();
            return {true, false, {}, true};
        }
        return {false, false, {}, false};
    }

    // Down / Left / Right enter caret-editing over the whole pre-edit (the caret
    // lands at the end, then this very key moves it / opens candidates). If there
    // is nothing pending the arrow falls through to the application.
    if ((sym == FcitxKey_Down || sym == FcitxKey_Left ||
         sym == FcitxKey_Right) &&
        !forcedEnglish_) {
        return enterSelection(key);
    }

    if (int kp = keypadAscii(sym)) {
        return handleChar(static_cast<char>(kp), /*literal=*/true);
    }
    if (sym >= 33 && sym <= 126) {
        return handleChar(static_cast<char>(sym));
    }

    return {false, false, {}, false};
}

// ---------------------------------------------------------------------------
// Typing path
// ---------------------------------------------------------------------------

KeyResult Buffer::handleChar(char c, bool literal) {
    // Forced pure-English mode: everything is literal text in the English tail.
    if (forcedEnglish_) {
        englishBuf_.push_back(c);
        return {true, false, {}, true};
    }

    // Literal keys (numeric keypad) are digits/symbols, never 注音: they end any
    // in-progress syllable and append to the English tail. (Main-row digits still
    // go through 注音 below — only the keypad sets `literal`.)
    if (literal) {
        if (token_ == Token::English) {
            englishBuf_.push_back(c);
            return {true, false, {}, true};
        }
        return flipToEnglish(c); // breaks a pending Chinese syllable, if any
    }

    if (token_ == Token::English) {
        // English→Chinese transition without a delimiter: only a tone key, by
        // completing a trailing 注音 syllable, peels Chinese off the tail.
        if (isToneChar(c)) {
            KeyResult peeled;
            if (tryPeelEnglish(c, peeled)) {
                return peeled;
            }
        }
        englishBuf_.push_back(c);
        return {true, false, {}, true};
    }

    int s = slotOf(c);

    // Non-注音 printable (punctuation, uppercase, ...): break to English.
    if (s < 0) {
        // Full-width mode: route punctuation keys through chewing's native
        // Chinese punctuation; everything else (letters) still breaks to English.
        if (fullWidthPunct_) {
            std::string punct = chinesePunct(c);
            if (!punct.empty()) {
                freezeAll();
                cells_.push_back({false, punct, {}});
                return {true, false, {}, true};
            }
        }
        return flipToEnglish(c);
    }

    if (syl_.empty()) {
        if (s == 3) {
            return flipToEnglish(c); // a tone cannot start a syllable
        }
        syl_.push_back(c);
        return {true, false, {}, true}; // raw until a tone completes it
    }

    // Does this key's class collide with one already in the in-progress
    // syllable? The syllable is unsealed (no completed character yet), so a
    // collision is ambiguous — it could continue this syllable or start a new
    // one — and we fall back to English (matches 大千 mixed-input behaviour).
    std::array<bool, 4> seen{false, false, false, false};
    for (char k : syl_) {
        int ks = slotOf(k);
        if (ks >= 0) {
            seen[ks] = true;
        }
    }
    if (seen[s]) {
        return flipToEnglish(c);
    }

    syl_.push_back(c);
    // Only turn raw keys into Chinese once they form a complete, toned syllable.
    // A toned-but-incomplete state like "s3" (ㄋˇ) stays raw so an out-of-order
    // vowel can still complete it ("s3" + u -> 你).
    const std::string body = canonical(syl_);
    if (syllableConverts(body)) {
        integrateSyllable(body);
        syl_.clear();
    }
    return {true, false, {}, true};
}

void Buffer::integrateSyllable(const std::string &body) {
    if (!englishBuf_.empty()) {
        // English sits in front of this new syllable: it can't merge with the
        // earlier run, so freeze "run + English" into cells_ and start fresh.
        freezeRun();
        freezeEnglish();
        zhuyin_.feedSequence(body);
        runReadings_ = {body};
    } else if (zhuyin_.hasConverted()) {
        // Extend the live run so chewing's phrasing spans it.
        for (char k : body) {
            zhuyin_.handleDefault(static_cast<int>(k));
        }
        runReadings_.push_back(body);
    } else {
        zhuyin_.feedSequence(body);
        runReadings_ = {body};
    }
    normalizeRunToTop();
    moveAutoCommit();
    token_ = Token::Chinese;
}

void Buffer::normalizeRunToTop() {
    if (!zhuyin_.hasConverted()) {
        return;
    }
    int n = utf8Count(zhuyin_.preedit());
    int pos = 0;
    for (int guard = 0; pos < n && guard < 64; ++guard) {
        zhuyin_.closeCandidates();
        zhuyin_.handleHome();
        for (int i = 0; i < pos; ++i) {
            zhuyin_.handleRight();
        }
        if (!zhuyin_.openCandidates() || zhuyin_.candidateCount() <= 0) {
            zhuyin_.closeCandidates();
            break;
        }
        // candidate(0) is the first item of the longest interval (page 0, index 0).
        // Only re-pick multi-character phrases: chewing's per-character auto-choice
        // is contextually better than the single-char candidate[0] (e.g. ㄕˋ
        // auto-converts to 是, but candidate[0] is 市). Phrases are where the list
        // order actually wins (你好 over the auto-picked 妳好).
        int len = utf8Count(zhuyin_.candidate(0));
        if (len >= 2) {
            zhuyin_.chooseCandidate(0);
            pos += len;
        } else {
            ++pos; // leave this single character as chewing's auto-conversion
        }
    }
    zhuyin_.closeCandidates();
    zhuyin_.handleEnd(); // keep appending at the tail for continued live typing
}

KeyResult Buffer::flipToEnglish(char trailing) {
    // No commit: the in-progress syllable's raw keys plus the breaking key become
    // the live English tail, sitting after the (still live) chewing run.
    englishBuf_ += syl_;
    englishBuf_.push_back(trailing);
    syl_.clear();
    token_ = Token::English;
    return {true, false, {}, true};
}

bool Buffer::tryPeelEnglish(char tone, KeyResult &out) {
    const std::string &buf = englishBuf_;
    // Prefer the shortest trailing syllable (largest k) that actually forms a
    // character: steal as few letters as possible from the English word. This
    // keeps brand names like "acer" intact ("aceru/6" -> acer + 螢) without
    // relying on a dictionary, which would miss non-words.
    for (std::size_t k = buf.size(); k-- > 0;) {
        std::string body = buf.substr(k);
        if (!validSyllable(body, /*allowTone=*/false)) {
            continue;
        }
        std::string prefix = buf.substr(0, k);
        std::string syllable = canonical(body);
        syllable.push_back(tone);
        if (!syllableConverts(syllable)) {
            continue;
        }

        // Freeze the live run plus the English prefix into cells_, then start a
        // fresh Chinese run with the peeled syllable — all still in the pre-edit.
        freezeRun();
        for (char c : prefix) {
            cells_.push_back({false, std::string(1, c), {}});
        }
        zhuyin_.feedSequence(syllable);
        runReadings_ = {syllable};
        moveAutoCommit();
        token_ = Token::Chinese;
        englishBuf_.clear();
        syl_.clear();
        out = {true, false, {}, true};
        return true;
    }
    return false;
}

KeyResult Buffer::handleSpace() {
    // A pending bopomofo syllable: space is its 一聲. Convert it if that yields a
    // character (a lone 聲母 like "t" does not — fall through to a literal space).
    if (!forcedEnglish_ && token_ == Token::Chinese && !syl_.empty()) {
        const std::string body = canonical(syl_);
        if (validSyllable(body, /*allowTone=*/false) &&
            syllableConvertsTone1(body)) {
            if (!englishBuf_.empty()) {
                freezeRun();
                freezeEnglish();
                zhuyin_.feedSequence(body);
                runReadings_.clear();
            } else if (zhuyin_.hasConverted()) {
                for (char k : body) {
                    zhuyin_.handleDefault(static_cast<int>(k));
                }
            } else {
                zhuyin_.feedSequence(body);
                runReadings_.clear();
            }
            zhuyin_.handleSpace();             // 一聲
            runReadings_.push_back(body + " "); // ' ' marks a 一聲 reading
            normalizeRunToTop();
            moveAutoCommit();
            syl_.clear();
            token_ = Token::Chinese;
            return {true, false, {}, true};
        }
    }
    // Otherwise space is a literal separator: fold the current token into cells_
    // and append a space. Still no commit — only Enter commits.
    freezeAll();
    cells_.push_back({false, " ", {}});
    return {true, false, {}, true};
}

KeyResult Buffer::handleEnter() {
    // The one and only commit point: flush the whole pre-edit to the client.
    std::string out = preeditText();
    if (out.empty()) {
        return {false, false, {}, false}; // nothing pending: let Enter through
    }
    mergeTail();      // gather the live tail + parked tail into cells_ for learning
    learnFromCells(); // teach chewing the chosen readings -> characters
    reset();
    return {true, true, out, true};
}

void Buffer::learnFromCells() {
    int i = 0;
    while (i < static_cast<int>(cells_.size())) {
        if (!cells_[i].chinese) {
            ++i;
            continue;
        }
        int s = i;
        while (i < static_cast<int>(cells_.size()) && cells_[i].chinese) {
            ++i;
        }
        int e = i - 1;

        // Rebuild the run in chewing from its readings, then force every cell to
        // the user's chosen text so the commit teaches chewing those choices
        // rather than its own defaults.
        feedRun(s, e, 0);
        relockRun(s, e, /*onlyLocked=*/false);

        zhuyin_.handleEnter(); // chewing's Enter commits AND runs autoLearn
        zhuyin_.takeCommit();  // ack and discard (output came from cells_)
    }
    zhuyin_.resetAll();
}

KeyResult Buffer::handleBackspace() {
    // Peel back through the live tail, then the run, then the finalized cells.
    if (!syl_.empty()) {
        syl_.pop_back();
        return {true, false, {}, true};
    }
    if (!englishBuf_.empty()) {
        englishBuf_.pop_back();
        if (englishBuf_.empty() && !forcedEnglish_) {
            token_ = Token::Chinese;
        }
        return {true, false, {}, true};
    }
    if (!zhuyin_.preedit().empty()) {
        zhuyin_.handleBackspace();
        if (!runReadings_.empty()) {
            runReadings_.pop_back();
        }
        return {true, false, {}, true};
    }
    if (!cells_.empty()) {
        cells_.pop_back();
        return {true, false, {}, true};
    }
    return {false, false, {}, false};
}

// ---------------------------------------------------------------------------
// Selection mode
// ---------------------------------------------------------------------------

int Buffer::feedRun(int start, int end, int offset) {
    zhuyin_.resetAll();
    for (int j = start; j <= end; ++j) {
        auto [body, tone1] = readingBody(cells_[j].reading);
        for (char c : body) {
            zhuyin_.handleDefault(static_cast<int>(c));
        }
        if (tone1) {
            zhuyin_.handleSpace(); // 一聲
        }
    }
    // Align the run to the top candidates first (same "以選字候選為準" rule as live
    // typing), then restore the user's explicit picks the fresh feed reverted.
    normalizeRunToTop();
    relockRun(start, end, /*onlyLocked=*/true);
    // Park the edit cursor on the character we want candidates for.
    zhuyin_.handleHome();
    for (int i = 0; i < offset; ++i) {
        zhuyin_.handleRight();
    }
    return end - start + 1;
}

void Buffer::relockRun(int start, int end, bool onlyLocked) {
    for (int j = start; j <= end; ++j) {
        if (onlyLocked && !cells_[j].locked) {
            continue; // leave un-pinned characters to chewing's phrasing
        }
        int off = j - start;
        auto chars = splitUtf8(zhuyin_.preedit());
        if (!onlyLocked && off < static_cast<int>(chars.size()) &&
            chars[off] == cells_[j].text) {
            continue; // already the chosen character; nothing to force
        }
        zhuyin_.closeCandidates();
        zhuyin_.handleHome();
        for (int k = 0; k < off; ++k) {
            zhuyin_.handleRight();
        }
        zhuyin_.openCandidates();
        for (int d = 0; d < inputer::kMaxSyllables; ++d) { // collapse to single chars
            if (zhuyin_.candidateCount() <= 0 ||
                utf8Count(zhuyin_.candidate(0)) <= 1) {
                break;
            }
            zhuyin_.handleDown();
        }
        int total = zhuyin_.candidateCount();
        int found = -1;
        for (int idx = 0; idx < total; ++idx) {
            if (zhuyin_.candidate(idx) == cells_[j].text) {
                found = idx;
                break;
            }
        }
        if (found < 0) {
            zhuyin_.closeCandidates();
            continue;
        }
        chooseGlobalCandidate(found);
    }
}

void Buffer::chineseRunAround(int idx, int &start, int &end) const {
    start = idx;
    end = idx;
    while (start - 1 >= 0 && cells_[start - 1].chinese) {
        --start;
    }
    while (end + 1 < static_cast<int>(cells_.size()) && cells_[end + 1].chinese) {
        ++end;
    }
}

void Buffer::chooseGlobalCandidate(int globalIdx) {
    int perPage = zhuyin_.candPerPage();
    int targetPage = perPage > 0 ? globalIdx / perPage : 0;
    while (zhuyin_.candCurrentPage() < targetPage) {
        zhuyin_.nextPage();
    }
    zhuyin_.chooseCandidate(perPage > 0 ? globalIdx % perPage : globalIdx);
}

void Buffer::buildSelCands() {
    selCands_.clear();
    selPage_ = 0;
    highlight_ = 0;
    zhuyin_.closeCandidates();
    zhuyin_.openCandidates();
    // Longest phrase interval first, then shorter, down to single characters —
    // exactly chewing's word-priority order, flattened into one list.
    for (int down = 0, guard = 0; guard < inputer::kMaxSyllables; ++guard, ++down) {
        int total = zhuyin_.candidateCount();
        if (total <= 0) {
            break;
        }
        int charLen = utf8Count(zhuyin_.candidate(0));
        for (int i = 0; i < total; ++i) {
            selCands_.push_back({zhuyin_.candidate(i), down, i});
        }
        if (charLen <= 1) {
            break; // reached the single-character interval
        }
        zhuyin_.handleDown(); // shorten the interval (phrase -> ... -> single)
    }
    // Last entry: revert this character to its raw 注音 keys (English). down = -1
    // marks it; picking it explodes the cell instead of choosing a homophone.
    std::string raw = readingBody(cells_[selCursor_].reading).first;
    if (!raw.empty()) {
        selCands_.push_back({raw, -1, -1});
    }
}

void Buffer::applyRunToCells() {
    auto chars = splitUtf8(zhuyin_.preedit());
    for (int j = selRunStart_; j <= selRunEnd_; ++j) {
        int k = j - selRunStart_;
        if (k < static_cast<int>(chars.size())) {
            cells_[j].text = chars[k];
        }
    }
}

void Buffer::loadCellCandidates() {
    selCands_.clear();
    selPage_ = 0;
    highlight_ = 0;
    if (!cells_[selCursor_].chinese) {
        // English cell: no candidates. Keep any loaded run intact (with its
        // locks) so returning to it preserves earlier picks.
        return;
    }
    int s, e;
    chineseRunAround(selCursor_, s, e);
    int offset = selCursor_ - s;
    if (!runLoaded_ || s != selRunStart_ || e != selRunEnd_) {
        // A different run (or first time): feed it fresh.
        selRunStart_ = s;
        selRunEnd_ = e;
        feedRun(s, e, offset);
        runLoaded_ = true;
    } else {
        // Same run already in chewing — just reposition the cursor so previously
        // locked picks survive (re-feeding would discard them).
        zhuyin_.closeCandidates();
        zhuyin_.handleHome();
        for (int i = 0; i < offset; ++i) {
            zhuyin_.handleRight();
        }
    }
    buildSelCands();
}

KeyResult Buffer::enterSelection(const fcitx::Key &key) {
    mergeTail(); // reunite any mid-string insertion before re-opening editing
    if (cells_.empty()) {
        // Nothing pending: let the arrow key reach the application.
        return {false, false, {}, false};
    }
    // Enter caret mode with the caret at the very end, then let the triggering
    // key (←/→/↓) act: ← steps it left, ↓ opens candidates on the last char.
    selecting_ = true;
    candOpen_ = false;
    caretPos_ = static_cast<int>(cells_.size());
    return handleSelecting(key);
}

void Buffer::exitSelection() {
    selecting_ = false;
    candOpen_ = false;
    caretPos_ = 0;
    highlight_ = 0;
    selPage_ = 0;
    selCands_.clear();
    runLoaded_ = false;
    zhuyin_.resetAll();
}

KeyResult Buffer::moveSelCursor(int delta) {
    int i = selCursor_ + delta;
    if (i < 0 || i >= static_cast<int>(cells_.size())) {
        return {true, false, {}, true}; // at an edge: stay put
    }
    selCursor_ = i; // step one cell at a time over every character, Chinese or not
    runLoaded_ = false;
    loadCellCandidates();
    return {true, false, {}, true};
}

KeyResult Buffer::pickCandidate(int pageIndex) {
    int gi = selPage_ * inputer::kCandPerPage + pageIndex;
    if (gi < 0 || gi >= static_cast<int>(selCands_.size())) {
        candOpen_ = false; // no such candidate: fall back to caret mode
        caretPos_ = selCursor_;
        return {true, false, {}, true};
    }
    SelCand sc = selCands_[gi];
    if (sc.down < 0) {
        return revertCellToEnglish(); // the "raw keys" entry
    }
    // Choose on the LIVE run (no re-feed) so chewing keeps earlier picks locked.
    // Reposition the cursor, walk to the candidate's interval, and choose it. A
    // phrase pick rewrites several cells, a single-character pick just one;
    // applyRun copies chewing's buffer back over the run's cells either way.
    int offset = selCursor_ - selRunStart_;
    zhuyin_.closeCandidates();
    zhuyin_.handleHome();
    for (int i = 0; i < offset; ++i) {
        zhuyin_.handleRight();
    }
    zhuyin_.openCandidates();
    for (int i = 0; i < sc.down; ++i) {
        zhuyin_.handleDown();
    }
    chooseGlobalCandidate(sc.idx);
    applyRunToCells();
    // Pin the characters the pick decided (one cell for a single, several for a
    // phrase) so re-opening selection later restores them (see relockRun).
    int picked = utf8Count(sc.text);
    for (int j = selCursor_; j < selCursor_ + picked &&
                            j <= selRunEnd_ && j < static_cast<int>(cells_.size());
         ++j) {
        cells_[j].locked = true;
    }
    // Advance past the characters this pick decided (one cell for a single,
    // several for a phrase). If a character follows, keep the candidate window
    // open on it so consecutive characters can be fixed; otherwise drop back to
    // caret mode with the caret parked right after the picked text.
    int next = selCursor_ + picked;
    if (next < static_cast<int>(cells_.size())) {
        selCursor_ = next;
        caretPos_ = next;
        runLoaded_ = false;
        loadCellCandidates();
        return {true, false, {}, true};
    }
    candOpen_ = false;
    caretPos_ = static_cast<int>(cells_.size());
    return {true, false, {}, true};
}

void Buffer::mergeTail() {
    freezeAll(); // fold the live typing at the insertion point into cells_ first
    if (!tail_.empty()) {
        cells_.insert(cells_.end(), tail_.begin(), tail_.end());
        tail_.clear();
    }
}

KeyResult Buffer::beginInsert(int pos, const fcitx::Key &key) {
    // Park the cell at `pos` and everything after it as the tail, then drop out
    // of editing and resume the normal typing path right there. The keystroke
    // composes exactly as it would at the end of the line; its result lands
    // before the parked tail, which reconnects on commit / re-selection.
    if (pos < 0) {
        pos = 0;
    }
    if (pos > static_cast<int>(cells_.size())) {
        pos = static_cast<int>(cells_.size());
    }
    tail_.assign(cells_.begin() + pos, cells_.end());
    cells_.erase(cells_.begin() + pos, cells_.end());
    exitSelection();         // selecting_ = false; sel scratch + zhuyin_ reset
    token_ = Token::Chinese; // a fresh syllable context at the insertion point
    return handleAuto(key);  // compose the key as ordinary input
}

KeyResult Buffer::revertCellToEnglish() {
    std::string reading = cells_[selCursor_].reading;
    if (!reading.empty() && reading.back() == ' ') {
        reading.pop_back(); // drop the 一聲 sentinel; the body is the raw keys
    }
    if (reading.empty()) {
        return {true, false, {}, true};
    }
    cells_.erase(cells_.begin() + selCursor_);
    int at = selCursor_;
    for (char c : reading) {
        cells_.insert(cells_.begin() + at, {false, std::string(1, c), {}});
        ++at;
    }
    runLoaded_ = false;   // cell layout changed
    candOpen_ = false;    // back to caret mode, caret right after the exploded keys
    caretPos_ = at;
    selCursor_ = at - 1;
    return {true, false, {}, true};
}

KeyResult Buffer::reinterpretFromCell() {
    // Accumulate raw keys from the cursor cell forward (English cells contribute
    // their letter; a Chinese cell contributes its reading) until they form a
    // complete, convertible syllable. Look only a few cells ahead.
    std::string raw;
    int consumeTo = -1;
    std::string found;
    int limit = std::min(static_cast<int>(cells_.size()), selCursor_ + 4);
    for (int j = selCursor_; j < limit; ++j) {
        std::string keys = cells_[j].chinese
                               ? readingBody(cells_[j].reading).first
                               : cells_[j].text;
        std::string trial = canonical(raw + keys);
        if (!validSyllable(trial, /*allowTone=*/true)) {
            break; // this cell can't be part of the syllable; stop
        }
        raw += keys;
        if (syllableConverts(trial)) {
            consumeTo = j;
            found = trial;
            break;
        }
    }
    if (consumeTo < 0) {
        return {true, false, {}, true}; // nothing forms a syllable: no-op
    }
    // Replace the consumed cells with a single Chinese cell, then open its
    // candidates so the user can confirm or pick another homophone.
    cells_.erase(cells_.begin() + selCursor_, cells_.begin() + consumeTo + 1);
    cells_.insert(cells_.begin() + selCursor_, {true, {}, found});
    runLoaded_ = false; // cell layout changed
    loadCellCandidates();
    // Fill the new cell's text from chewing's converted buffer (one character at
    // the cursor offset), not the candidate list (whose top item may be a phrase).
    auto chars = splitUtf8(zhuyin_.preedit());
    int k = selCursor_ - selRunStart_;
    cells_[selCursor_].text =
        k < static_cast<int>(chars.size()) ? chars[k] : found;
    return {true, false, {}, true};
}

KeyResult Buffer::handleSelecting(const fcitx::Key &key) {
    // Two sub-modes: a bare caret between characters (insert / navigate) and the
    // candidate window over one cell (re-pick). Number keys only ever pick in the
    // latter, so 注音 starting with a number-row key inserts cleanly in the former.
    return candOpen_ ? handlePicking(key) : handleCaret(key);
}

KeyResult Buffer::handleCaret(const fcitx::Key &key) {
    auto sym = key.sym();
    const int N = static_cast<int>(cells_.size());

    // Arrows move the caret between characters.
    if (sym == FcitxKey_Left) {
        if (caretPos_ > 0) {
            --caretPos_;
        }
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_Right) {
        if (caretPos_ < N) {
            ++caretPos_;
        }
        return {true, false, {}, true};
    }
    // ↓ / ↑ open the candidate window for the character LEFT of the caret (the
    // one just passed); ↑ additionally reinterprets an English cell as 注音.
    if (sym == FcitxKey_Down || sym == FcitxKey_Up) {
        int cell = caretPos_ > 0 ? caretPos_ - 1 : 0;
        return openCandidatesAt(cell, /*reinterpret=*/sym == FcitxKey_Up);
    }
    if (sym == FcitxKey_BackSpace) {
        // Delete the character left of the caret (ordinary editing).
        if (caretPos_ > 0) {
            cells_.erase(cells_.begin() + caretPos_ - 1);
            --caretPos_;
        }
        if (cells_.empty()) {
            exitSelection();
        }
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_Escape) {
        exitSelection(); // keep the text; just leave editing
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
        exitSelection();      // leave editing; the pre-edit stays in cells_
        return handleEnter(); // Enter is still the only commit point
    }
    // Any printable key (digits included) inserts at the caret as ordinary input
    // — this is what makes mid-string 注音 like 這/段 work.
    if (sym == FcitxKey_space || keypadAscii(sym) || (sym >= 33 && sym <= 126)) {
        return beginInsert(caretPos_, key);
    }
    // Other control keys: leave editing and let the application handle them.
    exitSelection();
    return handleAuto(key);
}

KeyResult Buffer::openCandidatesAt(int cell, bool reinterpret) {
    if (cell < 0 || cell >= static_cast<int>(cells_.size())) {
        return {true, false, {}, true};
    }
    selCursor_ = cell;
    runLoaded_ = false;
    if (cells_[cell].chinese) {
        candOpen_ = true;
        loadCellCandidates();
        return {true, false, {}, true};
    }
    // English cell: only ↑ acts — fold it (+ the next few) back into a 注音
    // character and open its candidates. ↓ on English has nothing to pick.
    if (reinterpret) {
        KeyResult r = reinterpretFromCell();
        candOpen_ = !selCands_.empty(); // false if nothing converted
        return r;
    }
    return {true, false, {}, true};
}

KeyResult Buffer::handlePicking(const fcitx::Key &key) {
    auto sym = key.sym();

    // ←/→ step to the adjacent character's candidates (fix several in a row).
    if (sym == FcitxKey_Left) {
        return moveSelCursor(-1);
    }
    if (sym == FcitxKey_Right) {
        return moveSelCursor(+1);
    }
    if (sym == FcitxKey_Escape) {
        candOpen_ = false; // close the window, back to caret mode on this cell
        caretPos_ = selCursor_;
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
        return pickCandidate(highlight_);
    }

    const int total = static_cast<int>(selCands_.size());
    const int totalPages =
        (total + inputer::kCandPerPage - 1) / inputer::kCandPerPage;
    int pageCount = std::min(inputer::kCandPerPage,
                             total - selPage_ * inputer::kCandPerPage);

    // ↓ (and space) move the highlight through the merged phrase→single list,
    // wrapping across pages at the end.
    if (sym == FcitxKey_Down || sym == FcitxKey_space) {
        if (highlight_ + 1 < pageCount) {
            ++highlight_;
        } else if (selPage_ + 1 < totalPages) {
            ++selPage_;
            highlight_ = 0;
        } else {
            selPage_ = 0;
            highlight_ = 0; // wrap to the very first candidate
        }
        return {true, false, {}, true};
    }
    // ↑ moves the highlight back through the merged list (does NOT revert here —
    // reverting is the last candidate entry, "raw keys"). Wraps at the top.
    if (sym == FcitxKey_Up) {
        if (highlight_ > 0) {
            --highlight_;
        } else if (selPage_ > 0) {
            --selPage_;
            highlight_ =
                std::min(inputer::kCandPerPage, total - selPage_ * inputer::kCandPerPage) - 1;
        } else {
            selPage_ = totalPages > 0 ? totalPages - 1 : 0;
            highlight_ =
                std::min(inputer::kCandPerPage, total - selPage_ * inputer::kCandPerPage) - 1;
        }
        if (highlight_ < 0) {
            highlight_ = 0;
        }
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_Page_Down) {
        if (selPage_ + 1 < totalPages) {
            ++selPage_;
        }
        highlight_ = 0;
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_Page_Up) {
        if (selPage_ > 0) {
            --selPage_;
        }
        highlight_ = 0;
        return {true, false, {}, true};
    }

    // Pick directly by number (main row or numeric keypad).
    int digit = -1;
    if (sym >= FcitxKey_1 && sym <= FcitxKey_9) {
        digit = sym - FcitxKey_1;
    } else if (int kp = keypadAscii(sym); kp >= '1' && kp <= '9') {
        digit = kp - '1';
    }
    if (digit >= 0) {
        if (digit < pageCount) {
            return pickCandidate(digit);
        }
        return {true, false, {}, false};
    }

    // Any other printable key: close the window and start inserting before this
    // character, composed as normal input. Non-printable control keys leave
    // editing and re-process.
    if (keypadAscii(sym) || (sym >= 33 && sym <= 126)) {
        return beginInsert(selCursor_, key);
    }
    candOpen_ = false;
    caretPos_ = selCursor_;
    return handleAuto(key);
}
