// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "layout.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <chewing.h>

namespace inputer {
namespace {

char foldBopomofoKey(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

KeyboardLayout &currentLayoutStorage() {
    static KeyboardLayout layout = KeyboardLayout::Default;
    return layout;
}

template <std::size_t N>
bool containsSymbol(const std::array<std::string_view, N> &symbols,
                    std::string_view s) {
    return std::find(symbols.begin(), symbols.end(), s) != symbols.end();
}

int slotForBopomofo(std::string_view s) {
    static constexpr std::array<std::string_view, 21> kInitials{
        "ㄅ", "ㄆ", "ㄇ", "ㄈ", "ㄉ", "ㄊ", "ㄋ", "ㄌ", "ㄍ", "ㄎ", "ㄏ",
        "ㄐ", "ㄑ", "ㄒ", "ㄓ", "ㄔ", "ㄕ", "ㄖ", "ㄗ", "ㄘ", "ㄙ"};
    static constexpr std::array<std::string_view, 3> kMedials{"ㄧ", "ㄨ", "ㄩ"};
    static constexpr std::array<std::string_view, 13> kFinals{
        "ㄚ", "ㄛ", "ㄜ", "ㄝ", "ㄞ", "ㄟ", "ㄠ",
        "ㄡ", "ㄢ", "ㄣ", "ㄤ", "ㄥ", "ㄦ"};
    static constexpr std::array<std::string_view, 4> kTones{"ˇ", "ˋ", "ˊ", "˙"};

    if (containsSymbol(kInitials, s)) {
        return 0;
    }
    if (containsSymbol(kMedials, s)) {
        return 1;
    }
    if (containsSymbol(kFinals, s)) {
        return 2;
    }
    if (containsSymbol(kTones, s)) {
        return kToneSlot;
    }
    return kNoZhuyinSlot;
}

std::array<int8_t, 128> buildSlots(KeyboardLayout layout) {
    std::array<int8_t, 128> slots{};
    slots.fill(kNoZhuyinSlot);

    ChewingContext *ctx = chewing_new2(nullptr, nullptr, nullptr, nullptr);
    if (!ctx) {
        return slots;
    }
    chewing_set_KBType(ctx, chewingKeyboardType(layout));
    for (int c = 33; c <= 126; ++c) {
        chewing_Reset(ctx);
        chewing_handle_Default(ctx, c);
        if (const char *bpmf = chewing_bopomofo_String_static(ctx);
            bpmf && *bpmf) {
            slots[static_cast<unsigned char>(foldBopomofoKey(
                static_cast<char>(c)))] = slotForBopomofo(bpmf);
        }
    }
    chewing_delete(ctx);
    return slots;
}

bool isDualRoleToneKey(KeyboardLayout layout, char c) {
    c = foldBopomofoKey(c);
    switch (layout) {
    case KeyboardLayout::Hsu:
        return c == 'd' || c == 'f' || c == 'j';
    case KeyboardLayout::Default:
    case KeyboardLayout::Eten:
    case KeyboardLayout::Ibm:
    case KeyboardLayout::GinYieh:
    case KeyboardLayout::Dvorak:
    case KeyboardLayout::Carpalx:
    case KeyboardLayout::ColemakDhAnsi:
    case KeyboardLayout::ColemakDhOrth:
    case KeyboardLayout::Workman:
    case KeyboardLayout::Colemak:
        return false;
    }
    return false;
}

const std::array<int8_t, 128> &slotsFor(KeyboardLayout layout) {
    // Keep each table behind its own case so the common default layout does not
    // pay to probe every libchewing keyboard at startup.
    switch (layout) {
    case KeyboardLayout::Default: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Default);
        return kSlots;
    }
    case KeyboardLayout::Eten: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Eten);
        return kSlots;
    }
    case KeyboardLayout::Hsu: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Hsu);
        return kSlots;
    }
    case KeyboardLayout::Ibm: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Ibm);
        return kSlots;
    }
    case KeyboardLayout::GinYieh: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::GinYieh);
        return kSlots;
    }
    case KeyboardLayout::Dvorak: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Dvorak);
        return kSlots;
    }
    case KeyboardLayout::Carpalx: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Carpalx);
        return kSlots;
    }
    case KeyboardLayout::ColemakDhAnsi: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::ColemakDhAnsi);
        return kSlots;
    }
    case KeyboardLayout::ColemakDhOrth: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::ColemakDhOrth);
        return kSlots;
    }
    case KeyboardLayout::Workman: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Workman);
        return kSlots;
    }
    case KeyboardLayout::Colemak: {
        static const std::array<int8_t, 128> kSlots =
            buildSlots(KeyboardLayout::Colemak);
        return kSlots;
    }
    }
    static const std::array<int8_t, 128> kFallback =
        buildSlots(KeyboardLayout::Default);
    return kFallback;
}

int baseSlot(KeyboardLayout layout, char c) {
    unsigned char uc = static_cast<unsigned char>(foldBopomofoKey(c));
    const auto &slots = slotsFor(layout);
    return uc < slots.size() ? slots[uc] : kNoZhuyinSlot;
}

bool hasBaseSlot(KeyboardLayout layout, int slot) {
    const auto &slots = slotsFor(layout);
    return std::any_of(slots.begin(), slots.end(),
                       [slot](int s) { return s == slot; });
}

bool hasToneSlot(KeyboardLayout layout) {
    if (hasBaseSlot(layout, kToneSlot)) {
        return true;
    }
    switch (layout) {
    case KeyboardLayout::Hsu:
        return isDualRoleToneKey(layout, 'd') ||
               isDualRoleToneKey(layout, 'f') ||
               isDualRoleToneKey(layout, 'j');
    case KeyboardLayout::Default:
    case KeyboardLayout::Eten:
    case KeyboardLayout::Ibm:
    case KeyboardLayout::GinYieh:
    case KeyboardLayout::Dvorak:
    case KeyboardLayout::Carpalx:
    case KeyboardLayout::ColemakDhAnsi:
    case KeyboardLayout::ColemakDhOrth:
    case KeyboardLayout::Workman:
    case KeyboardLayout::Colemak:
        return false;
    }
    return false;
}

bool assignSlotsRec(KeyboardLayout layout, const std::string &keys,
                    std::size_t index, bool allowTone,
                    std::array<bool, 4> &seen, std::vector<int> &assigned) {
    if (index == keys.size()) {
        return true;
    }

    char c = keys[index];
    std::array<int, 2> options{baseSlot(layout, c), kNoZhuyinSlot};
    if (keys.size() > 1 && isDualRoleToneKey(layout, c)) {
        // Try the contextual tone interpretation first so Hsu "nef" becomes
        // ㄋㄧˇ, while a bare "f" still starts ㄈ.
        options = {kToneSlot, baseSlot(layout, c)};
    }

    for (int s : options) {
        if (s < 0 || s >= static_cast<int>(seen.size())) {
            continue;
        }
        if (s == kToneSlot && !allowTone) {
            continue;
        }
        if (seen[s]) {
            continue;
        }
        seen[s] = true;
        assigned[index] = s;
        if (assignSlotsRec(layout, keys, index + 1, allowTone, seen, assigned)) {
            return true;
        }
        assigned[index] = kNoZhuyinSlot;
        seen[s] = false;
    }
    return false;
}

bool assignSlots(const std::string &keys, bool allowTone,
                 std::vector<int> &assigned) {
    KeyboardLayout layout = currentKeyboardLayout();
    assigned.assign(keys.size(), kNoZhuyinSlot);
    std::array<bool, 4> seen{false, false, false, false};
    return assignSlotsRec(layout, keys, 0, allowTone, seen, assigned);
}

} // namespace

KeyboardLayout currentKeyboardLayout() { return currentLayoutStorage(); }

void setCurrentKeyboardLayout(KeyboardLayout layout) {
    currentLayoutStorage() = layout;
}

const char *keyboardLayoutName(KeyboardLayout layout) {
    switch (layout) {
    case KeyboardLayout::Default: return "大千";
    case KeyboardLayout::Eten: return "倚天";
    case KeyboardLayout::Hsu: return "許氏";
    case KeyboardLayout::Ibm: return "IBM";
    case KeyboardLayout::GinYieh: return "精業";
    case KeyboardLayout::Dvorak: return "Dvorak";
    case KeyboardLayout::Carpalx: return "Carpalx";
    case KeyboardLayout::ColemakDhAnsi: return "Colemak-DH ANSI";
    case KeyboardLayout::ColemakDhOrth: return "Colemak-DH Ortholinear";
    case KeyboardLayout::Workman: return "Workman";
    case KeyboardLayout::Colemak: return "Colemak";
    }
    return "Unknown";
}

int chewingKeyboardType(KeyboardLayout layout) {
    switch (layout) {
    case KeyboardLayout::Default: return KB_DEFAULT;
    case KeyboardLayout::Eten: return KB_ET;
    case KeyboardLayout::Hsu: return KB_HSU;
    case KeyboardLayout::Ibm: return KB_IBM;
    case KeyboardLayout::GinYieh: return KB_GIN_YIEH;
    case KeyboardLayout::Dvorak: return KB_DVORAK;
    case KeyboardLayout::Carpalx: return KB_CARPALX;
    case KeyboardLayout::ColemakDhAnsi: return KB_COLEMAK_DH_ANSI;
    case KeyboardLayout::ColemakDhOrth: return KB_COLEMAK_DH_ORTH;
    case KeyboardLayout::Workman: return KB_WORKMAN;
    case KeyboardLayout::Colemak: return KB_COLEMAK;
    }
    return KB_DEFAULT;
}

bool keyboardLayoutAvailable(KeyboardLayout layout) {
    return hasBaseSlot(layout, 0) && hasBaseSlot(layout, 1) &&
           hasBaseSlot(layout, 2) && hasToneSlot(layout);
}

int zhuyinSlot(char c) {
    return baseSlot(currentKeyboardLayout(), c);
}

bool isToneKey(char c) {
    return zhuyinSlot(c) == kToneSlot ||
           isDualRoleToneKey(currentKeyboardLayout(), c);
}

std::string canonicalKeys(const std::string &keys) {
    std::string out = keys;
    for (char &c : out) {
        c = foldBopomofoKey(c);
    }
    std::vector<int> slots;
    if (!assignSlots(out, /*allowTone=*/true, slots)) {
        return out;
    }
    std::vector<std::size_t> order(out.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a,
                                                     std::size_t b) {
        return slots[a] < slots[b];
    });
    std::string sorted;
    sorted.reserve(out.size());
    for (std::size_t i : order) {
        sorted.push_back(out[i]);
    }
    return sorted;
}

bool isValidSyllable(const std::string &keys, bool allowTone) {
    std::vector<int> slots;
    return assignSlots(keys, allowTone, slots);
}

bool hasMedialOrFinal(const std::string &keys) {
    std::vector<int> slots;
    if (!assignSlots(keys, /*allowTone=*/true, slots)) {
        return false;
    }
    return std::any_of(slots.begin(), slots.end(),
                       [](int s) { return s == 1 || s == 2; });
}

bool needsBodyBeforeToneCompletion(KeyboardLayout layout) {
    switch (layout) {
    case KeyboardLayout::Hsu: return true;
    case KeyboardLayout::Default:
    case KeyboardLayout::Eten: return false;
    case KeyboardLayout::Ibm:
    case KeyboardLayout::GinYieh:
    case KeyboardLayout::Dvorak:
    case KeyboardLayout::Carpalx:
    case KeyboardLayout::ColemakDhAnsi:
    case KeyboardLayout::ColemakDhOrth:
    case KeyboardLayout::Workman:
    case KeyboardLayout::Colemak:
        return false;
    }
    return false;
}

} // namespace inputer
