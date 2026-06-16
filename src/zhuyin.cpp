// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "zhuyin.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <chewing.h>

#include "constants.h"

namespace {

// Resolve (and create) the per-user dictionary path so chewing can persist
// learned phrase frequencies across sessions. Returns empty on failure, in
// which case chewing falls back to its default user path.
std::string userPath() {
    namespace fs = std::filesystem;
    fs::path base;
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        base = fs::path(xdg);
    } else if (const char *home = std::getenv("HOME"); home && *home) {
        base = fs::path(home) / ".config";
    } else {
        return {};
    }
    fs::path dir = base / "inputer";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        return {};
    }
    // chewing_new2 expects a FILE path for the user dictionary, not a directory.
    return (dir / "userdict.dat").string();
}

} // namespace

Zhuyin::Zhuyin() {
    const std::string path = userPath();
    ctx_ = chewing_new2(nullptr, path.empty() ? nullptr : path.c_str(), nullptr,
                        nullptr);
    if (!ctx_) {
        return;
    }
    chewing_set_KBType(ctx_, KB_DEFAULT);     // 大千 (Taiwan standard) layout.
    chewing_set_autoLearn(ctx_, AUTOLEARN_ENABLED); // Learn homophone choices.
    chewing_set_spaceAsSelection(ctx_, 0);    // We drive selection ourselves.
    chewing_set_escCleanAllBuf(ctx_, 1);
    chewing_set_candPerPage(ctx_, inputer::kCandPerPage);
    chewing_set_maxChiSymbolLen(ctx_, 20);
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
    chewing_ack(ctx_);
    return out;
}
