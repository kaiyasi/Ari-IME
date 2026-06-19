// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#ifndef INPUTER_ZHUYIN_H
#define INPUTER_ZHUYIN_H

#include <string>
#include <vector>

#include "layout.h"

struct ChewingContext;

// Thin RAII wrapper around a libchewing context, configured from the current
// keyboard layout. The engine feeds raw QWERTY keys; chewing maps them to
// bopomofo via that layout and performs candidate lookup, intelligent phrasing
// and automatic per-user learning.
class Zhuyin {
public:
    Zhuyin();
    ~Zhuyin();

    Zhuyin(const Zhuyin &) = delete;
    Zhuyin &operator=(const Zhuyin &) = delete;

    // Clear all internal buffers but keep settings + learned user dictionary.
    void resetAll();
    void setKeyboardLayout(inputer::KeyboardLayout layout);

    // Feed a single raw key (a printable ASCII character, e.g. 's', 'u', '3').
    void feedKey(char c);

    // Reset, then feed every key in the sequence. Use to (re)interpret a whole
    // English buffer as bopomofo, e.g. "su3" -> ㄋㄧˇ.
    void feedSequence(const std::string &keys);

    // True when chewing has converted bopomofo into Chinese characters sitting
    // in the pre-edit buffer (i.e. a complete syllable was formed).
    bool hasConverted() const;
    // True when there is a pending (incomplete) bopomofo syllable.
    bool hasBopomofo() const;
    bool hasAnything() const { return hasConverted() || hasBopomofo(); }

    // Converted characters followed by any pending bopomofo, for pre-edit
    // display (e.g. "你" or "ㄋㄧ").
    std::string preedit() const;

    // --- Key forwarding (中文模式 / 注音優先): drive chewing directly. ---
    void handleDefault(int key); // printable ASCII -> bopomofo / selection
    void handleSpace();
    void handleEnter();
    void handleEsc();
    void handleBackspace();
    void handleDelete();
    void handleUp();
    void handleDown();
    void handleLeft();
    void handleRight();
    void handleHome();
    void handleEnd();

    // Whether the last forwarded keystroke was absorbed / ignored by chewing.
    // When ignored, the engine should let the application handle the key.
    bool absorbed() const;
    bool ignored() const;

    // Candidate selection window control.
    bool openCandidates();
    void closeCandidates();
    int candidateCount() const;
    std::string candidate(int index) const;
    void chooseCandidate(int index);
    // Candidates on chewing's current page only (≤ candPerPage), so number keys
    // 1-9 line up with what chewing will select.
    std::vector<std::string> pageCandidates() const;

    // Current edit-cursor position (in characters) within the converted buffer.
    int cursorPos() const;

    // Pagination info / control for a manually-highlighted candidate cursor.
    int candPerPage() const;
    int candCurrentPage() const;
    int candTotalPage() const;
    void nextPage();
    void prevPage();

    // Drop the pending (incomplete) bopomofo syllable, keeping converted chars.
    void cleanBopomofo();

    // Force whatever is in the pre-edit buffer into the commit buffer.
    void forceCommitPreedit();

    bool hasCommit() const;
    // Returns the committed string and acknowledges chewing's output buffers.
    std::string takeCommit();

private:
    ChewingContext *ctx_ = nullptr;
};

#endif // INPUTER_ZHUYIN_H
