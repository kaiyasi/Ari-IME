// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#ifndef INPUTER_LAYOUT_H
#define INPUTER_LAYOUT_H

#include <fcitx-config/enum.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/i18n.h>

#include <string>
#include <vector>

namespace inputer {

inline constexpr int kNoZhuyinSlot = -1;
inline constexpr int kToneSlot = 3;

enum class KeyboardLayout {
    Default,
    Eten,
    Hsu,
    Ibm,
    GinYieh,
    Dvorak,
    Carpalx,
    ColemakDhAnsi,
    ColemakDhOrth,
    Workman,
    Colemak,
};
FCITX_CONFIG_ENUM_NAME(KeyboardLayout, "Default", "Eten", "Hsu", "Ibm",
                       "GinYieh", "Dvorak", "Carpalx", "ColemakDhAnsi",
                       "ColemakDhOrth", "Workman", "Colemak");

// Modifier policy for the temporary Chinese-punctuation gesture. The
// punctuation key itself remains the normal physical key, so this setting can
// avoid an application shortcut without introducing another toolbar or mode.
enum class ChinesePunctuationShortcut {
    ControlShift,
    AltShift,
    Control,
    Alt,
    Disabled,
};
FCITX_CONFIG_ENUM_NAME(ChinesePunctuationShortcut, "ControlShift", "AltShift",
                       "Control", "Alt", "Disabled");

struct SyllableKeySequence {
    std::string keys;
    bool toneOne = false;
};

struct KeyboardLayoutI18NAnnotation {
    bool skipDescription() const { return false; }
    bool skipSave() const { return false; }
    void dumpDescription(fcitx::RawConfig &config) const {
        config.setValueByPath("EnumI18n/0", _("大千"));
        config.setValueByPath("EnumI18n/1", _("倚天"));
        config.setValueByPath("EnumI18n/2", _("許氏"));
        config.setValueByPath("EnumI18n/3", _("IBM"));
        config.setValueByPath("EnumI18n/4", _("精業"));
        config.setValueByPath("EnumI18n/5", _("Dvorak"));
        config.setValueByPath("EnumI18n/6", _("Carpalx"));
        config.setValueByPath("EnumI18n/7", _("Colemak-DH ANSI"));
        config.setValueByPath("EnumI18n/8", _("Colemak-DH Ortholinear"));
        config.setValueByPath("EnumI18n/9", _("Workman"));
        config.setValueByPath("EnumI18n/10", _("Colemak"));
    }
};

struct ChinesePunctuationShortcutI18NAnnotation {
    bool skipDescription() const { return false; }
    bool skipSave() const { return false; }
    void dumpDescription(fcitx::RawConfig &config) const {
        config.setValueByPath("EnumI18n/0", _("Ctrl+Shift"));
        config.setValueByPath("EnumI18n/1", _("Alt+Shift"));
        config.setValueByPath("EnumI18n/2", _("Ctrl"));
        config.setValueByPath("EnumI18n/3", _("Alt"));
        config.setValueByPath("EnumI18n/4", _("停用"));
    }
};

// Current keyboard layout: 大千 / libchewing KB_DEFAULT.
//
// A syllable has at most one key from each slot, in canonical order:
// 聲母 < 介音 < 韻母 < 聲調.
KeyboardLayout currentKeyboardLayout();
void setCurrentKeyboardLayout(KeyboardLayout layout);
const char *keyboardLayoutName(KeyboardLayout layout);
int chewingKeyboardType(KeyboardLayout layout);
bool keyboardLayoutAvailable(KeyboardLayout layout);
int zhuyinSlot(char c);
bool isToneKey(char c);
bool isSymbolLikeZhuyinKey(char c);
std::string canonicalKeys(const std::string &keys);
bool isValidSyllable(const std::string &keys, bool allowTone);
bool hasMedialOrFinal(const std::string &keys);
bool needsBodyBeforeToneCompletion(KeyboardLayout layout);
// Enumerate valid raw keyboard sequences for a layout. The result is used only
// for safe reverse lookup (reconversion); it is deliberately kept separate
// from the normal typing parser so ordinary keystrokes never pay the
// enumeration cost.
std::vector<SyllableKeySequence> syllableKeySequences(KeyboardLayout layout);

} // namespace inputer

#endif // INPUTER_LAYOUT_H
