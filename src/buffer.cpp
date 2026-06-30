// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "buffer.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <fcitx-utils/keysym.h>

#include "constants.h"
#include "layout.h"

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

bool isAsciiControl(const std::string &ch) {
    if (ch.size() != 1) {
        return false;
    }
    unsigned char c = static_cast<unsigned char>(ch[0]);
    return c < 0x20 || c == 0x7F;
}

bool isPasteSeparator(const std::string &ch) {
    return isAsciiControl(ch) || ch == "\xc2\xa0" || ch == "\xe2\x80\xa8" ||
           ch == "\xe2\x80\xa9" || ch == "\xe2\x80\xaf" ||
           ch == "\xe3\x80\x80";
}

bool isIgnoredPasteFormat(const std::string &ch) {
    return ch == "\xe2\x80\x8b" || ch == "\xe2\x81\xa0" ||
           ch == "\xef\xbb\xbf";
}

bool isAsciiWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
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

fcitx::KeySym normalizeKeySym(fcitx::KeySym sym) {
    switch (sym) {
    case FcitxKey_KP_Space: return FcitxKey_space;
    case FcitxKey_KP_Home: return FcitxKey_Home;
    case FcitxKey_KP_Left: return FcitxKey_Left;
    case FcitxKey_KP_Up: return FcitxKey_Up;
    case FcitxKey_KP_Right: return FcitxKey_Right;
    case FcitxKey_KP_Down: return FcitxKey_Down;
    case FcitxKey_KP_Prior: return FcitxKey_Page_Up;
    case FcitxKey_KP_Next: return FcitxKey_Page_Down;
    case FcitxKey_KP_End: return FcitxKey_End;
    case FcitxKey_KP_Begin: return FcitxKey_Begin;
    case FcitxKey_KP_Insert: return FcitxKey_Insert;
    case FcitxKey_KP_Delete: return FcitxKey_Delete;
    default: return sym;
    }
}

bool hasWordModifier(const fcitx::Key &key) {
    return key.states().testAny(fcitx::KeyStates{
        fcitx::KeyState::Ctrl, fcitx::KeyState::Alt, fcitx::KeyState::Super});
}

Zhuyin *probeForCurrentLayout(Zhuyin *&probe,
                              inputer::KeyboardLayout &probeLayout) {
    inputer::KeyboardLayout layout = inputer::currentKeyboardLayout();
    if (!probe || probeLayout != layout) {
        delete probe;
        probe = new Zhuyin();
        probeLayout = layout;
    }
    return probe;
}

// Whether a complete (toned) canonical syllable converts to a Chinese character
// with nothing left dangling.
bool syllableConverts(const std::string &canonicalKeys) {
    // Intentionally leaked at process exit: a chewing context must not be torn
    // down during static destruction. It is rebuilt only on explicit layout
    // changes while the process is alive.
    static Zhuyin *probe = nullptr;
    static inputer::KeyboardLayout probeLayout = inputer::KeyboardLayout::Default;
    probe = probeForCurrentLayout(probe, probeLayout);
    probe->feedSequence(canonicalKeys);
    return probe->hasConverted() && !probe->hasBopomofo();
}

// Whether a bopomofo body (no tone) yields a character under 一聲 (the tone the
// space key applies). Used to decide if space should convert a pending syllable.
bool syllableConvertsTone1(const std::string &canonicalBody) {
    static Zhuyin *probe = nullptr;
    static inputer::KeyboardLayout probeLayout = inputer::KeyboardLayout::Default;
    probe = probeForCurrentLayout(probe, probeLayout);
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
    if (c == '\\') {
        return "、";
    }
    if (c == '^') {
        return "……";
    }
    switch (c) {
    case '@': return "＠";
    case '#': return "＃";
    case '$': return "＄";
    case '%': return "％";
    case '&': return "＆";
    case '*': return "＊";
    case '+': return "＋";
    case '=': return "＝";
    case '|': return "｜";
    case '~': return "～";
    case '_': return "＿";
    case '`': return "｀";
    case '"': return "＂";
    case '\'': return "＇";
    default: break;
    }
    // Intentionally leaked, like the syllable probes above.
    static Zhuyin *probe = nullptr;
    static inputer::KeyboardLayout probeLayout = inputer::KeyboardLayout::Default;
    probe = probeForCurrentLayout(probe, probeLayout);
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

bool isAsciiLower(char c) {
    return c >= 'a' && c <= 'z';
}

bool hasAsciiLetter(const std::string &s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    });
}

bool isSingleAsciiLowerCell(const std::string &text) {
    return text.size() == 1 && isAsciiLower(text[0]);
}

bool isTechnicalLiteralSuffix(const std::string &text) {
    return text == "." || text == "/" || text == ":" || text == "," ||
           text == "=" ||
           text == "+" || text == "*" || text == "?" || text == "{" ||
           text == "}" || text == "[" || text == "]" || text == "|";
}

bool canPeelEnglishBody(const std::string &prefix, const std::string &body) {
    int nonToneSlots = 0;
    bool hasInitial = false;
    bool hasMedial = false;
    for (char c : body) {
        int slot = inputer::zhuyinSlot(c);
        if (slot < 0 || slot == inputer::kToneSlot) {
            continue;
        }
        ++nonToneSlots;
        if (slot == 0) {
            hasInitial = true;
        } else if (slot == 1) {
            hasMedial = true;
        }
    }
    // English text contains plenty of punctuation/digit patterns that overlap
    // with final-only bopomofo keys, such as ".3" in version numbers. Do not
    // peel digit/punctuation-only bodies from English.
    if (!hasAsciiLetter(body)) {
        return false;
    }
    if (!hasInitial && nonToneSlots < 2) {
        // A single medial can be a legitimate English-word-friendly peel
        // ("catsu3" -> "cats以"), but avoid acronym+zhuyin cases where the
        // lowercase consonant before it belongs to the syllable ("HTTPsu3").
        return hasMedial && body.size() == 1 && prefix.size() >= 2 &&
               isAsciiLower(prefix.back()) &&
               isAsciiLower(prefix[prefix.size() - 2]);
    }
    if (!prefix.empty() && !body.empty() && isAsciiLower(prefix.back()) &&
        isAsciiLower(body.front()) && inputer::zhuyinSlot(body.front()) == 0) {
        return false;
    }
    return true;
}

bool canPeelSymbolLedBody(const std::string &body) {
    if (body.size() < 2 || !inputer::isSymbolLikeZhuyinKey(body.front())) {
        return false;
    }
    return std::any_of(body.begin() + 1, body.end(), [](char c) {
        return inputer::zhuyinSlot(c) >= 0;
    });
}

bool hasAsciiDigit(const std::string &s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        return c >= '0' && c <= '9';
    });
}

} // namespace

void Buffer::reset() {
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

bool Buffer::setKeyboardLayout(inputer::KeyboardLayout layout) {
    if (layout_ == layout) {
        return false;
    }
    // Readings stored in existing cells are layout-specific raw keys. If the
    // user changes layouts mid-preedit, keep the invariant simple and avoid
    // re-feeding old readings through a different layout.
    reset();
    layout_ = layout;
    inputer::setCurrentKeyboardLayout(layout_);
    zhuyin_.setKeyboardLayout(layout_);
    return true;
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
    int count = visibleCandidateCount();
    if (count <= 0) {
        return out; // caret mode shows no candidate window
    }
    out.reserve(count);
    int start = selPage_ * inputer::kCandPerPage;
    for (int i = start; i < start + count; ++i) {
        out.push_back(selCands_[i].display);
    }
    return out;
}

KeyResult Buffer::selectCandidate(int pageIndex) {
    if (!selecting_ || !candOpen_) {
        return {false, false, {}, false};
    }
    if (pageIndex < 0 || pageIndex >= visibleCandidateCount()) {
        return {true, false, {}, false};
    }
    return pickCandidate(pageIndex);
}

int Buffer::candidatePage() const {
    return (!selecting_ || !candOpen_ || selCands_.empty()) ? 0 : selPage_ + 1;
}

int Buffer::candidatePageCount() const {
    if (!selecting_ || !candOpen_ || selCands_.empty()) {
        return 0;
    }
    return (static_cast<int>(selCands_.size()) + inputer::kCandPerPage - 1) /
           inputer::kCandPerPage;
}

int Buffer::highlight() const {
    return visibleCandidateCount() <= 0 ? -1 : highlight_;
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
    if (normalizeKeySym(key.sym()) == FcitxKey_space &&
        key.states().test(fcitx::KeyState::Ctrl)) {
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

    auto sym = normalizeKeySym(key.sym());

    if (sym == FcitxKey_space) {
        return handleSpace();
    }
    if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
        return handleEnter();
    }
    if (sym == FcitxKey_BackSpace) {
        return handleBackspace();
    }
    if (sym == FcitxKey_Delete) {
        return handleDelete();
    }
    if (sym == FcitxKey_Escape) {
        if (!preeditText().empty()) {
            reset();
            return {true, false, {}, true};
        }
        return {false, false, {}, false};
    }

    // Navigation keys enter caret-editing over the whole pre-edit (the caret
    // lands at the end, then this very key moves it / opens candidates). If
    // there is nothing pending the key falls through to the application.
    if ((sym == FcitxKey_Down || sym == FcitxKey_Left ||
         sym == FcitxKey_Right || sym == FcitxKey_Up ||
         sym == FcitxKey_Home || sym == FcitxKey_Begin ||
         sym == FcitxKey_End) &&
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
        return handleLiteralChar(c); // breaks a pending Chinese syllable, if any
    }

    if (token_ == Token::English) {
        // In an English token, some punctuation-looking keys are still valid
        // bopomofo pieces for tail peeling (e.g. "/" in "aceru/6" -> 螢). Only
        // convert keys that are not part of the active keyboard layout.
        if (fullWidthPunct_ && inputer::zhuyinSlot(c) < 0) {
            std::string punct = chinesePunct(c);
            if (!punct.empty()) {
                freezeRun();
                freezeEnglish();
                cells_.push_back({false, punct, {}});
                token_ = Token::Chinese;
                return {true, false, {}, true};
            }
        }
        // English→Chinese transition without a delimiter: only a tone key, by
        // completing a trailing 注音 syllable, peels Chinese off the tail.
        if (inputer::isToneKey(c)) {
            KeyResult peeled;
            if (tryPeelEnglish(c, peeled)) {
                return peeled;
            }
        }
        englishBuf_.push_back(c);
        return {true, false, {}, true};
    }

    int s = inputer::zhuyinSlot(c);

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
        return handleLiteralChar(c);
    }

    if (syl_.empty()) {
        if (s == 3) {
            return handleLiteralChar(c); // a tone cannot start a syllable
        }
        if (shouldPreferLiteralAmbiguousStart(c)) {
            return handleLiteralChar(c);
        }
        syl_.push_back(c);
        return {true, false, {}, true}; // raw until a tone completes it
    }

    // If the extended raw keys cannot be assigned to a valid syllable, the
    // unsealed hypothesis was wrong and we fall back to English. This goes
    // through the layout layer because some layouts have dual-role keys (Hsu
    // 'f' is ㄈ at syllable start but ˇ after a body).
    if (!inputer::isValidSyllable(syl_ + c, /*allowTone=*/true)) {
        return handleLiteralChar(c);
    }

    syl_.push_back(c);
    // Only turn raw keys into Chinese once they form a complete, toned syllable.
    // A toned-but-incomplete state like "s3" (ㄋˇ) stays raw so an out-of-order
    // vowel can still complete it ("s3" + u -> 你).
    const std::string body = inputer::canonicalKeys(syl_);
    const bool needsBody =
        inputer::needsBodyBeforeToneCompletion(inputer::currentKeyboardLayout());
    if ((!needsBody || inputer::hasMedialOrFinal(body)) &&
        syllableConverts(body)) {
        integrateSyllable(body);
        syl_.clear();
    }
    return {true, false, {}, true};
}

KeyResult Buffer::handleLiteralChar(char c) {
    if (fullWidthPunct_) {
        std::string punct = chinesePunct(c);
        if (!punct.empty()) {
            freezeAll();
            cells_.push_back({false, punct, {}});
            return {true, false, {}, true};
        }
    }
    if (token_ == Token::English) {
        englishBuf_.push_back(c);
        return {true, false, {}, true};
    }
    return flipToEnglish(c);
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

bool Buffer::shouldPreferLiteralAmbiguousStart(char c) const {
    if (!inputer::isSymbolLikeZhuyinKey(c)) {
        return false;
    }
    // Punctuation-looking zhuyin starts are too ambiguous to convert eagerly:
    // stage them as literal text first, then recover them later only if the
    // following keys prove a valid symbol-led syllable.
    return syl_.empty();
}

bool Buffer::cellLooksLiteralish(const Cell &cell) const {
    if (cell.chinese) {
        return false;
    }
    if (cell.text == " ") {
        return true;
    }
    if (cell.text.size() != 1) {
        return true;
    }
    unsigned char c = static_cast<unsigned char>(cell.text[0]);
    return isAsciiWhitespace(static_cast<char>(c)) || !std::isalnum(c);
}

int Buffer::literalContextBiasAt(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(cells_.size())) {
        return 0;
    }

    int bias = 0;
    const Cell &cell = cells_[idx];
    auto reading = readingBody(cell.reading).first;
    if (!reading.empty() && inputer::isSymbolLikeZhuyinKey(reading.front())) {
        bias += 3;
    }
    if (cell.locked) {
        bias += 1;
    }

    auto considerNeighbor = [this, &bias](const Cell *neighbor) {
        if (!neighbor) {
            ++bias;
            return;
        }
        if (neighbor->chinese) {
            --bias;
            return;
        }
        if (cellLooksLiteralish(*neighbor)) {
            bias += 2;
        } else {
            ++bias;
        }
    };

    considerNeighbor(idx > 0 ? &cells_[idx - 1] : nullptr);
    considerNeighbor(idx + 1 < static_cast<int>(cells_.size()) ? &cells_[idx + 1]
                                                               : nullptr);

    int literalNeighbors = 0;
    for (int j = std::max(0, idx - 2);
         j <= std::min(static_cast<int>(cells_.size()) - 1, idx + 2); ++j) {
        if (j == idx || cells_[j].chinese) {
            continue;
        }
        if (cellLooksLiteralish(cells_[j])) {
            ++literalNeighbors;
        }
    }
    if (literalNeighbors >= 2) {
        bias += 2;
    }

    return bias;
}

int Buffer::candidateScore(const SelCand &cand) const {
    const int literalBias = literalContextBiasAt(selCursor_);
    const bool hasChineseLeft = selCursor_ > 0 && cells_[selCursor_ - 1].chinese;
    bool hasChineseRight =
        selCursor_ + 1 < static_cast<int>(cells_.size()) &&
        cells_[selCursor_ + 1].chinese;
    if (cand.down < 0) {
        int score = -48 + literalBias * 10;
        if (hasChineseLeft) {
            score -= 12;
        }
        if (hasChineseRight) {
            score -= 12;
        }
        if (literalBias >= 5) {
            score += 18;
        }
        return score;
    }

    int len = utf8Count(cand.text);
    int score = 0;
    score += len * 16;
    score -= std::max(cand.down, 0) * 7;

    if (len == 1) {
        score += literalBias * 5;
    } else {
        score -= literalBias * 4;
    }

    hasChineseRight = selCursor_ + len < static_cast<int>(cells_.size()) &&
                      cells_[selCursor_ + len].chinese;
    if (len > 1 && (hasChineseLeft || hasChineseRight)) {
        score += 8;
    }
    if (len > 1 && hasChineseLeft && hasChineseRight) {
        score += 10;
    }
    if (len == 1 && !hasChineseLeft && !hasChineseRight) {
        score += 10;
    }
    if (len == 1 && hasChineseLeft && hasChineseRight) {
        score -= 8;
    }

    if (hasAsciiLetter(cand.text) || hasAsciiDigit(cand.text)) {
        score -= 20;
    }

    std::vector<std::string> chars = splitUtf8(cand.text);
    bool exactSpanMatch = true;
    int limit = std::min<int>(chars.size(),
                              static_cast<int>(cells_.size()) - selCursor_);
    for (int i = 0; i < limit; ++i) {
        const Cell &cell = cells_[selCursor_ + i];
        if (chars[i] != cell.text) {
            exactSpanMatch = false;
            if (cell.locked) {
                score -= 24;
            }
            if (!cell.chinese) {
                score -= 16;
            }
        }
    }
    if (exactSpanMatch && len == limit) {
        score += 28;
    }

    return score;
}

void Buffer::rankSelCands() {
    std::stable_sort(selCands_.begin(), selCands_.end(),
                     [this](const SelCand &a, const SelCand &b) {
                         int scoreA = candidateScore(a);
                         int scoreB = candidateScore(b);
                         if (scoreA != scoreB) {
                             return scoreA > scoreB;
                         }
                         if (a.down != b.down) {
                             return a.down < b.down;
                         }
                         return a.order < b.order;
                     });
}

bool Buffer::tryPeelEnglish(char tone, KeyResult &out) {
    const std::string &buf = englishBuf_;
    // A punctuation-looking key may have been kept literal at a boundary because
    // it was ambiguous on its own. If the trailing literal tail later forms a
    // clear symbol-led zhuyin body, recover that longer suffix first instead of
    // peeling only the shortest alphabetic tail.
    for (std::size_t k = 0; k < buf.size(); ++k) {
        std::string body = buf.substr(k);
        if (!inputer::isValidSyllable(body, /*allowTone=*/false) ||
            !canPeelSymbolLedBody(body)) {
            continue;
        }
        std::string syllable = inputer::canonicalKeys(body);
        syllable.push_back(tone);
        if (!syllableConverts(syllable)) {
            continue;
        }

        freezeRun();
        for (char c : buf.substr(0, k)) {
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

    // Prefer the shortest trailing syllable (largest k) that actually forms a
    // character: steal as few letters as possible from the English word. This
    // keeps brand names like "acer" intact ("aceru/6" -> acer + 螢) without
    // relying on a dictionary, which would miss non-words.
    for (std::size_t k = buf.size(); k-- > 0;) {
        std::string body = buf.substr(k);
        if (!inputer::isValidSyllable(body, /*allowTone=*/false)) {
            continue;
        }
        std::string prefix = buf.substr(0, k);
        if (!canPeelEnglishBody(prefix, body)) {
            continue;
        }
        std::string syllable = inputer::canonicalKeys(body);
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

bool Buffer::tryPeelEnglishTone1(KeyResult &out) {
    const std::string &buf = englishBuf_;
    for (std::size_t k = 0; k < buf.size(); ++k) {
        std::string body = buf.substr(k);
        if (!inputer::isValidSyllable(body, /*allowTone=*/false) ||
            !canPeelSymbolLedBody(body)) {
            continue;
        }
        std::string syllable = inputer::canonicalKeys(body);
        if (!syllableConvertsTone1(syllable)) {
            continue;
        }

        freezeRun();
        for (char c : buf.substr(0, k)) {
            cells_.push_back({false, std::string(1, c), {}});
        }
        zhuyin_.feedSequence(syllable);
        zhuyin_.handleSpace();
        runReadings_ = {syllable + " "};
        normalizeRunToTop();
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
        const std::string body = inputer::canonicalKeys(syl_);
        if (inputer::isValidSyllable(body, /*allowTone=*/false) &&
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
    if (!forcedEnglish_ && token_ == Token::English) {
        KeyResult peeled;
        if (tryPeelEnglishTone1(peeled)) {
            return peeled;
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
    if (!tail_.empty()) {
        // The caret is at the front of a parked tail during mid-string
        // insertion. Backspace at that boundary is a no-op, but must still be
        // absorbed so the application does not delete text outside the pre-edit.
        return {true, false, {}, true};
    }
    return {false, false, {}, false};
}

KeyResult Buffer::handleDelete() {
    // Delete is caret-relative. During normal end-of-string composition there is
    // nothing to the right, but while inserting mid-string the parked tail_ is
    // exactly the text to the right of the caret.
    if (!tail_.empty()) {
        tail_.erase(tail_.begin());
        return {true, false, {}, true};
    }
    if (!preeditText().empty()) {
        return {true, false, {}, false}; // absorb Delete at the end of pre-edit
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
            std::string text = zhuyin_.candidate(i);
            selCands_.push_back({text, text, down, i,
                                 static_cast<int>(selCands_.size())});
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
        selCands_.push_back(
            {raw, "原始鍵 " + raw, -1, -1, static_cast<int>(selCands_.size())});
    }
    rankSelCands();
}

int Buffer::visibleCandidateCount() const {
    if (!selecting_ || !candOpen_ || selCands_.empty()) {
        return 0;
    }
    int start = selPage_ * inputer::kCandPerPage;
    if (start < 0 || start >= static_cast<int>(selCands_.size())) {
        return 0;
    }
    return std::min(inputer::kCandPerPage,
                    static_cast<int>(selCands_.size()) - start);
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

void Buffer::pasteAtCaret(const std::string &text) {
    if (text.empty()) {
        return;
    }
    // Resolve a single insertion index, whatever state we're in: while editing,
    // the caret position; while typing, the live tail folds into cells_ and the
    // insertion point is just before any parked tail_.
    int pos;
    if (selecting_) {
        pos = candOpen_ ? selCursor_ : caretPos_;
    } else {
        freezeAll();
        pos = static_cast<int>(cells_.size());
        if (!tail_.empty()) {
            cells_.insert(cells_.end(), tail_.begin(), tail_.end());
            tail_.clear();
        }
    }
    if (pos < 0) {
        pos = 0;
    }
    if (pos > static_cast<int>(cells_.size())) {
        pos = static_cast<int>(cells_.size());
    }
    // Pasted text drops in as literal cells (no 注音 reading): it is finished
    // text, not something to re-pick. Keep the pre-edit single-line and safe for
    // clients by folding control/newline-like separators into one visible space.
    std::vector<Cell> pasted;
    for (const std::string &ch : splitUtf8(text)) {
        if (isIgnoredPasteFormat(ch)) {
            continue;
        }
        if (isPasteSeparator(ch)) {
            if (pasted.empty() || pasted.back().text != " ") {
                pasted.push_back({false, " ", {}});
            }
            continue;
        }
        pasted.push_back({false, ch, {}});
    }
    if (pasted.empty()) {
        return;
    }
    cells_.insert(cells_.begin() + pos, pasted.begin(), pasted.end());
    // Land in caret mode with the caret right after the pasted text, so further
    // keys keep composing at that position.
    selecting_ = true;
    candOpen_ = false;
    runLoaded_ = false;
    caretPos_ = pos + static_cast<int>(pasted.size());
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
    const std::string currentKeys = cells_[selCursor_].chinese
                                        ? readingBody(cells_[selCursor_].reading).first
                                        : cells_[selCursor_].text;
    const bool symbolLed =
        !currentKeys.empty() && inputer::isSymbolLikeZhuyinKey(currentKeys.front());
    if (selCursor_ > 0 && !symbolLed &&
        !isSingleAsciiLowerCell(cells_[selCursor_ - 1].text)) {
        return {true, false, {}, true};
    }
    std::string raw;
    int consumeTo = -1;
    std::string found;
    int bestScore = -1000000;
    int limit = std::min(static_cast<int>(cells_.size()), selCursor_ + 4);
    auto startsAsciiField = [this](int index) {
        if (index < 0 || index >= static_cast<int>(cells_.size())) {
            return false;
        }
        const std::string &text = cells_[index].text;
        return text.size() == 1 &&
               ((text[0] >= 'A' && text[0] <= 'Z') ||
                (text[0] >= 'a' && text[0] <= 'z') ||
                (text[0] >= '0' && text[0] <= '9'));
    };
    auto rawUsesSymbolKey = [](const std::string &keys) {
        return std::any_of(keys.begin(), keys.end(), [](char c) {
            return inputer::isSymbolLikeZhuyinKey(c);
        });
    };
    for (int j = selCursor_; j < limit; ++j) {
        std::string keys = cells_[j].chinese
                               ? readingBody(cells_[j].reading).first
                               : cells_[j].text;
        std::string trial = inputer::canonicalKeys(raw + keys);
        if (!inputer::isValidSyllable(trial, /*allowTone=*/true)) {
            break; // this cell can't be part of the syllable; stop
        }
        raw += keys;
        if ((hasAsciiLetter(raw) || canPeelSymbolLedBody(raw)) &&
            syllableConverts(trial)) {
            int next = j + 1;
            bool skippedSpace = false;
            while (next < static_cast<int>(cells_.size()) &&
                   cells_[next].text == " ") {
                skippedSpace = true;
                ++next;
            }
            const bool consumesSymbolKey = rawUsesSymbolKey(raw);
            if (!symbolLed && consumesSymbolKey && startsAsciiField(next)) {
                continue;
            }

            int score = 0;
            const int consumed = j - selCursor_ + 1;
            score += symbolLed ? consumed * 12 : -consumed * 4;
            if (selCursor_ > 0 && cells_[selCursor_ - 1].chinese) {
                score += 10;
            }
            if (next < static_cast<int>(cells_.size()) && cells_[next].chinese) {
                score += 10;
            }
            if (next < static_cast<int>(cells_.size()) &&
                (isTechnicalLiteralSuffix(cells_[next].text) ||
                 (skippedSpace && startsAsciiField(next)))) {
                score -= 48;
            }
            if (startsAsciiField(selCursor_ - 1) && !symbolLed) {
                score += 6;
            }
            if (score > bestScore) {
                bestScore = score;
                consumeTo = j;
                found = trial;
            }
        }
    }
    if (consumeTo < 0) {
        return {true, false, {}, true}; // nothing forms a syllable: no-op
    }
    int next = consumeTo + 1;
    bool skippedSpace = false;
    while (next < static_cast<int>(cells_.size()) && cells_[next].text == " ") {
        skippedSpace = true;
        ++next;
    }
    if (next < static_cast<int>(cells_.size()) &&
        (isTechnicalLiteralSuffix(cells_[next].text) ||
         (skippedSpace && startsAsciiField(next)))) {
        return {true, false, {}, true};
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
    auto sym = normalizeKeySym(key.sym());
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
    if (sym == FcitxKey_Home || sym == FcitxKey_Begin) {
        caretPos_ = 0;
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_End) {
        caretPos_ = N;
        return {true, false, {}, true};
    }
    // ↓ / ↑ open the candidate window for the character the caret points AT — the
    // one to its RIGHT (at the end, the last character); ↑ additionally
    // reinterprets an English cell as 注音.
    if (sym == FcitxKey_Down || sym == FcitxKey_Up) {
        int cell = caretPos_ < N ? caretPos_ : N - 1;
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
    if (sym == FcitxKey_Delete) {
        // Delete the character right of the caret.
        if (caretPos_ < N) {
            cells_.erase(cells_.begin() + caretPos_);
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
    auto sym = normalizeKeySym(key.sym());

    // ←/→ step to the adjacent character's candidates (fix several in a row).
    if (sym == FcitxKey_Left) {
        return moveSelCursor(-1);
    }
    if (sym == FcitxKey_Right) {
        return moveSelCursor(+1);
    }
    if (sym == FcitxKey_Home || sym == FcitxKey_Begin) {
        selCursor_ = 0;
        runLoaded_ = false;
        loadCellCandidates();
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_End) {
        selCursor_ = static_cast<int>(cells_.size()) - 1;
        runLoaded_ = false;
        loadCellCandidates();
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_BackSpace || sym == FcitxKey_Delete) {
        // In picking mode the focused cell is the user's editing target, so
        // remove that cell and leave the caret at its former position.
        if (selCursor_ >= 0 && selCursor_ < static_cast<int>(cells_.size())) {
            cells_.erase(cells_.begin() + selCursor_);
        }
        if (cells_.empty()) {
            exitSelection();
            return {true, false, {}, true};
        }
        candOpen_ = false;
        runLoaded_ = false;
        caretPos_ = std::min(selCursor_, static_cast<int>(cells_.size()));
        selCursor_ = std::min(caretPos_, static_cast<int>(cells_.size()) - 1);
        return {true, false, {}, true};
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

    const bool shift = key.states().test(fcitx::KeyState::Shift);

    // ↓ / Space / Tab move the highlight through the merged phrase→single list,
    // wrapping across pages at the end. Shift+Tab walks backward.
    if (sym == FcitxKey_Down || sym == FcitxKey_space ||
        (sym == FcitxKey_Tab && !shift)) {
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
    // ↑ / Shift+Tab move the highlight back through the merged list (does NOT
    // revert here — reverting is the last candidate entry, "raw keys"). Wraps
    // at the top. ISO_Left_Tab is what some toolkits send for Shift+Tab.
    if (sym == FcitxKey_Up || (sym == FcitxKey_Tab && shift) ||
        sym == FcitxKey_ISO_Left_Tab) {
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
    // character, composed as normal input. Non-printable control keys close the
    // window but keep caret mode, then pass through to the application.
    if (keypadAscii(sym) || (sym >= 33 && sym <= 126)) {
        return beginInsert(selCursor_, key);
    }
    candOpen_ = false;
    caretPos_ = selCursor_;
    return {false, false, {}, true};
}
