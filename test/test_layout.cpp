// Assertion-based regression tests for keyboard layout configuration and key
// classification. Exits non-zero on any failure so it can gate changes.

#include <string>
#include <cctype>

#include <fcitx-config/rawconfig.h>

#include <chewing.h>

#include "layout.h"
#include "test_common.h"

namespace {

using test::check;
using test::check_eq;

void test_layout_classification() {
    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);
    check(inputer::currentKeyboardLayout() == inputer::KeyboardLayout::Default,
          "layout defaults to KB_DEFAULT");
    check_eq(inputer::keyboardLayoutName(inputer::currentKeyboardLayout()), "大千",
             "layout reports display name");
    check(inputer::chewingKeyboardType(inputer::currentKeyboardLayout()) ==
              KB_DEFAULT,
          "layout maps to chewing KB_DEFAULT");
    check(inputer::keyboardLayoutAvailable(inputer::KeyboardLayout::Default),
          "Default layout has complete slot table");
    fcitx::RawConfig raw;
    marshallOption(raw, inputer::KeyboardLayout::Default);
    check_eq(raw.value(), "Default", "layout marshals config value");
    inputer::KeyboardLayout parsed = inputer::KeyboardLayout::Default;
    check(unmarshallOption(parsed, raw, false) &&
              parsed == inputer::KeyboardLayout::Default,
          "layout unmarshals config value");
    marshallOption(raw, inputer::KeyboardLayout::Eten);
    check_eq(raw.value(), "Eten", "layout marshals Eten config value");
    check(unmarshallOption(parsed, raw, false) &&
              parsed == inputer::KeyboardLayout::Eten,
          "layout unmarshals Eten config value");
    marshallOption(raw, inputer::KeyboardLayout::Hsu);
    check_eq(raw.value(), "Hsu", "layout marshals Hsu config value");
    check(unmarshallOption(parsed, raw, false) &&
              parsed == inputer::KeyboardLayout::Hsu,
          "layout unmarshals Hsu config value");
    fcitx::RawConfig desc;
    inputer::KeyboardLayoutI18NAnnotation().dumpDescription(desc);
    check_eq(desc.get("EnumI18n/0")->value(), "大千",
             "layout config shows 大千");
    check_eq(desc.get("EnumI18n/1")->value(), "倚天",
             "layout config shows 倚天");
    check_eq(desc.get("EnumI18n/2")->value(), "許氏",
             "layout config shows 許氏");
    check_eq(desc.get("EnumI18n/3")->value(), "IBM",
             "layout config shows IBM");
    check_eq(desc.get("EnumI18n/4")->value(), "精業",
             "layout config shows 精業");
    check_eq(desc.get("EnumI18n/7")->value(), "Colemak-DH ANSI",
             "layout config shows Colemak-DH ANSI");
    check(inputer::zhuyinSlot('s') == inputer::zhuyinSlot('S'),
          "layout folds uppercase zhuyin keys");
    check(inputer::zhuyinSlot('4') == inputer::kToneSlot,
          "layout classifies exact tone symbols");
    check(inputer::isToneKey('3'), "layout recognizes tone key");
    check(!inputer::isSymbolLikeZhuyinKey('s'),
          "letters are not treated as symbol-like zhuyin keys");
    check_eq(inputer::canonicalKeys("s3u"), "su3",
             "layout canonicalizes out-of-order syllable");
    check(inputer::isValidSyllable("su3", /*allowTone=*/true),
          "layout accepts complete syllable");
    check(!inputer::isValidSyllable("ssu3", /*allowTone=*/true),
          "layout rejects repeated slots");
    check(!inputer::isValidSyllable("su3", /*allowTone=*/false),
          "layout rejects tone when disabled");

    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Eten);
    check_eq(inputer::keyboardLayoutName(inputer::currentKeyboardLayout()), "倚天",
             "layout reports Eten display name");
    check(inputer::chewingKeyboardType(inputer::currentKeyboardLayout()) == KB_ET,
          "layout maps Eten to chewing KB_ET");
    check(inputer::keyboardLayoutAvailable(inputer::KeyboardLayout::Eten),
          "Eten layout has complete slot table");
    check(inputer::zhuyinSlot('e') == 1, "Eten e is medial ㄧ");
    check_eq(inputer::canonicalKeys("n3e"), "ne3",
             "Eten canonicalizes out-of-order syllable");

    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Hsu);
    check_eq(inputer::keyboardLayoutName(inputer::currentKeyboardLayout()), "許氏",
             "layout reports Hsu display name");
    check(inputer::chewingKeyboardType(inputer::currentKeyboardLayout()) == KB_HSU,
          "layout maps Hsu to chewing KB_HSU");
    check(inputer::keyboardLayoutAvailable(inputer::KeyboardLayout::Hsu),
          "Hsu layout has complete slot table");
    check(inputer::zhuyinSlot('f') == 0, "Hsu f starts as initial ㄈ");
    check(inputer::isToneKey('f'), "Hsu f is also a contextual tone key");
    check_eq(inputer::canonicalKeys("nfe"), "nef",
             "Hsu canonicalizes contextual tone out of order");
    check(inputer::isValidSyllable("nef", /*allowTone=*/true),
          "Hsu accepts dual-role tone in syllable");

    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);
}

void test_additional_layouts() {
    struct Case {
        inputer::KeyboardLayout layout;
        const char *name;
        int chewingType;
        char initial;
        char medial;
        char tone;
        const char *keys;
        const char *canonical;
    };
    const Case cases[] = {
        {inputer::KeyboardLayout::Ibm, "IBM", KB_IBM, '7', 'a', ',', "7a,",
         "7a,"},
        {inputer::KeyboardLayout::GinYieh, "精業", KB_GIN_YIEH, 'd', '-', 'a',
         "d-a", "d-a"},
        {inputer::KeyboardLayout::Dvorak, "Dvorak", KB_DVORAK, 'o', 'g', '3',
         "og3", "og3"},
        {inputer::KeyboardLayout::Carpalx, "Carpalx", KB_CARPALX, 's', 'u',
         '3', "su3", "su3"},
        {inputer::KeyboardLayout::ColemakDhAnsi, "Colemak-DH ANSI",
         KB_COLEMAK_DH_ANSI, 'r', 'l', '3', "rl3", "rl3"},
        {inputer::KeyboardLayout::ColemakDhOrth, "Colemak-DH Ortholinear",
         KB_COLEMAK_DH_ORTH, 'r', 'l', '3', "rl3", "rl3"},
        {inputer::KeyboardLayout::Workman, "Workman", KB_WORKMAN, 's', 'f',
         '3', "sf3", "sf3"},
        {inputer::KeyboardLayout::Colemak, "Colemak", KB_COLEMAK, 'r', 'l',
         '3', "rl3", "rl3"},
    };

    for (const auto &c : cases) {
        inputer::setCurrentKeyboardLayout(c.layout);
        std::string nameLabel = std::string(c.name) + " reports display name";
        check_eq(inputer::keyboardLayoutName(c.layout), c.name,
                 nameLabel.c_str());
        std::string typeLabel = std::string(c.name) + " maps to chewing type";
        check(inputer::chewingKeyboardType(c.layout) == c.chewingType,
              typeLabel.c_str());
        std::string availableLabel =
            std::string(c.name) + " has complete slot table";
        check(inputer::keyboardLayoutAvailable(c.layout),
              availableLabel.c_str());
        std::string initialLabel = std::string(c.name) + " classifies initial";
        check(inputer::zhuyinSlot(c.initial) == 0,
              initialLabel.c_str());
        std::string medialLabel = std::string(c.name) + " classifies medial";
        check(inputer::zhuyinSlot(c.medial) == 1,
              medialLabel.c_str());
        std::string toneLabel = std::string(c.name) + " classifies tone";
        check(inputer::zhuyinSlot(c.tone) == inputer::kToneSlot,
              toneLabel.c_str());
        std::string canonicalLabel =
            std::string(c.name) + " canonicalizes syllable";
        check_eq(inputer::canonicalKeys(c.keys), c.canonical,
                 canonicalLabel.c_str());
        if (!std::isalnum(static_cast<unsigned char>(c.medial))) {
            std::string symbolLabel =
                std::string(c.name) + " classifies punctuation-looking medial";
            check(inputer::isSymbolLikeZhuyinKey(c.medial),
                  symbolLabel.c_str());
        }
        if (!std::isalnum(static_cast<unsigned char>(c.tone))) {
            std::string symbolLabel =
                std::string(c.name) + " classifies punctuation-looking tone";
            check(inputer::isSymbolLikeZhuyinKey(c.tone),
                  symbolLabel.c_str());
        }
        fcitx::RawConfig raw;
        marshallOption(raw, c.layout);
        inputer::KeyboardLayout parsed = inputer::KeyboardLayout::Default;
        std::string configLabel = std::string(c.name) + " round-trips config";
        check(unmarshallOption(parsed, raw, false) && parsed == c.layout,
              configLabel.c_str());
    }

    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);
}

} // namespace

int main() {
    test::TempConfigHome configHome("inputer-layout-test-config");

    test_layout_classification();
    test_additional_layouts();

    return test::finish();
}
