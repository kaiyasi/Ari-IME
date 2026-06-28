// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
//
// libFuzzer harness for the Buffer state machine. Build with:
//   cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DINPUTER_ENABLE_FUZZING=ON
//   cmake --build build-fuzz --target fuzz_buffer
//   ./build-fuzz/fuzz_buffer -runs=1000

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include "buffer.h"
#include "constants.h"
#include "layout.h"
#include "test_common.h"

namespace {

struct FuzzState {
    Buffer buffer;
    std::string committed;

    void press(const fcitx::Key &key) {
        KeyResult result = buffer.handleKey(key);
        if (result.hasCommit) {
            committed += result.commitText;
        }
    }
};

bool validUtf8(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len = 0;
        if ((c & 0x80) == 0x00) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        } else {
            return false;
        }
        if (i + len > text.size()) {
            return false;
        }
        for (std::size_t j = 1; j < len; ++j) {
            unsigned char t = static_cast<unsigned char>(text[i + j]);
            if ((t & 0xC0) != 0x80) {
                return false;
            }
        }
        i += len;
    }
    return true;
}

int utf8Count(const std::string &text) {
    int count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

[[noreturn]] void failInvariant() {
    std::abort();
}

void checkInvariants(const FuzzState &state) {
    const std::string preedit = state.buffer.preeditText();
    if (!validUtf8(preedit) || !validUtf8(state.committed)) {
        failInvariant();
    }

    const int chars = utf8Count(preedit);
    const int caret = state.buffer.caretChar();
    if (caret < -1 || caret > chars) {
        failInvariant();
    }

    const int selection = state.buffer.selectionChar();
    if (selection < -1 || selection >= std::max(chars, 1)) {
        failInvariant();
    }

    const std::vector<std::string> candidates = state.buffer.candidates();
    if (candidates.size() > inputer::kCandPerPage) {
        failInvariant();
    }
    for (const std::string &candidate : candidates) {
        if (!validUtf8(candidate)) {
            failInvariant();
        }
    }

    const int highlight = state.buffer.highlight();
    if (candidates.empty()) {
        if (highlight != -1 || state.buffer.candidatePage() != 0 ||
            state.buffer.candidatePageCount() != 0) {
            failInvariant();
        }
    } else {
        if (highlight < 0 ||
            highlight >= static_cast<int>(candidates.size()) ||
            state.buffer.candidatePage() <= 0 ||
            state.buffer.candidatePageCount() < state.buffer.candidatePage()) {
            failInvariant();
        }
    }
}

fcitx::Key navigationKey(uint8_t value) {
    static constexpr fcitx::KeySym keys[] = {
        FcitxKey_BackSpace, FcitxKey_Delete,    FcitxKey_Left,
        FcitxKey_Right,     FcitxKey_Up,        FcitxKey_Down,
        FcitxKey_Home,      FcitxKey_End,       FcitxKey_Page_Up,
        FcitxKey_Page_Down, FcitxKey_Escape,    FcitxKey_Tab,
        FcitxKey_Return,    FcitxKey_KP_Enter,  FcitxKey_KP_1,
        FcitxKey_KP_2,      FcitxKey_KP_3,      FcitxKey_KP_Decimal,
    };
    return fcitx::Key(keys[value % (sizeof(keys) / sizeof(keys[0]))]);
}

inputer::KeyboardLayout layoutFor(uint8_t value) {
    static constexpr inputer::KeyboardLayout layouts[] = {
        inputer::KeyboardLayout::Default,
        inputer::KeyboardLayout::Eten,
        inputer::KeyboardLayout::Hsu,
        inputer::KeyboardLayout::Ibm,
        inputer::KeyboardLayout::GinYieh,
        inputer::KeyboardLayout::Dvorak,
        inputer::KeyboardLayout::Carpalx,
        inputer::KeyboardLayout::ColemakDhAnsi,
        inputer::KeyboardLayout::ColemakDhOrth,
        inputer::KeyboardLayout::Workman,
        inputer::KeyboardLayout::Colemak,
    };
    return layouts[value % (sizeof(layouts) / sizeof(layouts[0]))];
}

std::string pastePayload(const uint8_t *data, std::size_t size,
                         std::size_t &offset) {
    if (offset >= size) {
        return {};
    }
    const uint8_t lenByte = data[offset++];
    const std::size_t len =
        std::min<std::size_t>((lenByte % 12) + 1, size - offset);
    std::string text;
    text.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
        uint8_t b = data[offset++];
        if (b < 0x20) {
            text.push_back(static_cast<char>(b));
        } else {
            text.push_back(static_cast<char>(0x20 + (b % 0x5f)));
        }
    }
    return text;
}

void applyByte(FuzzState &state, const uint8_t *data, std::size_t size,
               std::size_t &offset) {
    const uint8_t op = data[offset++];
    if (op >= 0x20 && op <= 0x7e) {
        state.press(fcitx::Key(static_cast<fcitx::KeySym>(op)));
        return;
    }
    switch (op % 8) {
    case 0:
    case 1:
    case 2:
    case 3:
        state.press(fcitx::Key(static_cast<fcitx::KeySym>(0x20 + (op % 0x5f))));
        break;
    case 4:
        state.press(navigationKey(op));
        break;
    case 5:
        state.press(fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl));
        break;
    case 6:
        if (op & 0x80) {
            state.buffer.setFullWidthPunct(!state.buffer.isFullWidthPunct());
        } else {
            inputer::KeyboardLayout layout = layoutFor(op);
            inputer::setCurrentKeyboardLayout(layout);
            state.buffer.setKeyboardLayout(layout);
        }
        break;
    case 7:
        if (op & 0x40) {
            state.buffer.selectCandidate(op % inputer::kCandPerPage);
        } else {
            state.buffer.pasteAtCaret(pastePayload(data, size, offset));
        }
        break;
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
    static test::TempConfigHome configHome("inputer-buffer-fuzz-config");
    setenv("INPUTER_DISABLE_AUTOLEARN", "1", 1);

    FuzzState state;
    std::size_t offset = 0;
    while (offset < size) {
        applyByte(state, data, size, offset);
        checkInvariants(state);
    }
    return 0;
}
