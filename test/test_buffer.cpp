// Assertion-based regression tests for the Buffer state machine. Exits non-zero
// on any failure so it can gate changes.
//
// Build & run:
//   cmake --build build
//   ctest --test-dir build --output-on-failure

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include "buffer.h"
#include "constants.h"
#include "layout.h"
#include "test_common.h"

namespace {

using test::check;
using test::check_eq;

int utf8_count(const std::string &s) {
    int n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) {
            ++n;
        }
    }
    return n;
}

std::string utf8_char_at(const std::string &s, int charIndex) {
    int current = 0;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (current == charIndex && i + len <= s.size()) {
            return s.substr(i, len);
        }
        i += len;
        ++current;
    }
    return {};
}

bool valid_utf8(const std::string &s) {
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
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
        if (i + len > s.size()) {
            return false;
        }
        for (std::size_t j = 1; j < len; ++j) {
            unsigned char t = static_cast<unsigned char>(s[i + j]);
            if ((t & 0xC0) != 0x80) {
                return false;
            }
        }
        i += len;
    }
    return true;
}

// A tiny driver that feeds keys into one Buffer and accumulates committed text.
struct Sim {
    Buffer b;
    std::string committed;

    KeyResult press(const fcitx::Key &k) {
        KeyResult r = b.handleKey(k);
        if (r.hasCommit) {
            committed += r.commitText;
        }
        return r;
    }
    KeyResult press(fcitx::KeySym sym) {
        return press(fcitx::Key(sym));
    }
    void key(fcitx::KeySym sym) {
        press(sym);
    }
    void key(char c) { key(static_cast<fcitx::KeySym>(static_cast<unsigned char>(c))); }
    void type(const std::string &s) {
        for (char c : s) {
            key(c);
        }
    }
    std::string preedit() { return b.preeditText(); }
    std::vector<std::string> cand() { return b.candidates(); }
    std::vector<std::string> preview() { return b.previewCandidates(); }
};

std::string bu4_default() {
    Sim s;
    s.type("1j4");
    std::string out = s.preedit();
    check(valid_utf8(out), "1j4 default candidate is valid UTF-8");
    check(utf8_count(out) == 1, "1j4 converts to one Chinese character");
    check(out != "1j4", "1j4 does not remain raw keys");
    return out;
}

bool contains_han_character(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        if ((lead & 0x80) == 0) {
            codepoint = lead;
            length = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            codepoint = lead & 0x1F;
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            codepoint = lead & 0x0F;
            length = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            codepoint = lead & 0x07;
            length = 4;
        } else {
            ++i;
            continue;
        }
        if (i + length > text.size()) {
            break;
        }
        for (std::size_t j = 1; j < length; ++j) {
            codepoint =
                (codepoint << 6) |
                (static_cast<unsigned char>(text[i + j]) & 0x3F);
        }
        if ((codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
            (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
            (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
            (codepoint >= 0x20000 && codepoint <= 0x2FA1F)) {
            return true;
        }
        i += length;
    }
    return false;
}

// What libchewing itself converts an already-toned key sequence to on `layout`,
// with no Buffer in the loop. Both the keyboard tables and the phrase ranking
// belong to libchewing and differ between releases (see ISSUES.md), so a test
// that means "Ari routes these keys the way libchewing does" has to derive its
// expectation rather than pin a character that happened to be right on one
// distribution. Returns empty when the sequence does not convert, so callers
// can skip instead of asserting against nothing.
std::string direct_conversion(ari_ime::KeyboardLayout layout,
                              const std::string &keys) {
    Zhuyin direct;
    if (!direct.ok()) {
        return {};
    }
    direct.setKeyboardLayout(layout);
    std::string folded = keys;
    for (char &key : folded) {
        if (key >= 'A' && key <= 'Z') {
            key = static_cast<char>(key + ('a' - 'A'));
        }
    }
    direct.feedSequence(folded);
    const std::string out = direct.preedit();
    return contains_han_character(out) ? out : std::string{};
}

// Feed `keys` straight to libchewing as ONE run, with ' ' meaning the tone-1
// Space key, and no Buffer in between. Lets a test assert "Ari handed the whole
// run to libchewing" without also asserting which homophone libchewing picks —
// that ranking is libchewing's and moves between releases (see ISSUES.md).
std::string direct_run_conversion(ari_ime::KeyboardLayout layout,
                                  const std::string &keys) {
    Zhuyin direct;
    if (!direct.ok()) {
        return {};
    }
    direct.setKeyboardLayout(layout);
    direct.resetAll();
    for (char key : keys) {
        if (key == ' ') {
            direct.handleSpace();
            continue;
        }
        if (key >= 'A' && key <= 'Z') {
            key = static_cast<char>(key + ('a' - 'A'));
        }
        direct.feedKey(key);
    }
    const std::string out = direct.preedit();
    return contains_han_character(out) ? out : std::string{};
}

std::string direct_tone1_conversion(ari_ime::KeyboardLayout layout,
                                    const std::string &keys) {
    Zhuyin direct;
    if (!direct.ok()) {
        return {};
    }
    direct.setKeyboardLayout(layout);
    std::string folded = keys;
    for (char &key : folded) {
        if (key >= 'A' && key <= 'Z') {
            key = static_cast<char>(key + ('a' - 'A'));
        }
    }
    direct.feedSequence(folded);
    direct.handleSpace();
    const std::string out = direct.preedit();
    return contains_han_character(out) ? out : std::string{};
}

struct SymbolLeadCase {
    ari_ime::KeyboardLayout layout;
    std::string keys;
};

int find_visible_candidate(const std::vector<std::string> &cands,
                           const std::string &text) {
    for (int i = 0; i < static_cast<int>(cands.size()); ++i) {
        if (cands[i] == text) {
            return i;
        }
    }
    return -1;
}

bool find_symbol_lead_case(char lead, SymbolLeadCase &out) {
    const ari_ime::KeyboardLayout layouts[] = {
        ari_ime::KeyboardLayout::Default,
        ari_ime::KeyboardLayout::Eten,
        ari_ime::KeyboardLayout::Hsu,
        ari_ime::KeyboardLayout::Ibm,
        ari_ime::KeyboardLayout::GinYieh,
        ari_ime::KeyboardLayout::Dvorak,
        ari_ime::KeyboardLayout::Carpalx,
        ari_ime::KeyboardLayout::ColemakDhAnsi,
        ari_ime::KeyboardLayout::ColemakDhOrth,
        ari_ime::KeyboardLayout::Workman,
        ari_ime::KeyboardLayout::Colemak,
    };
    for (ari_ime::KeyboardLayout layout : layouts) {
        if (!ari_ime::keyboardLayoutAvailable(layout)) {
            continue;
        }
        ari_ime::setCurrentKeyboardLayout(layout);
        if (!ari_ime::isSymbolLikeZhuyinKey(lead)) {
            continue;
        }
        for (int mid = 33; mid <= 126; ++mid) {
            if (!ari_ime::isValidSyllable(std::string{lead, static_cast<char>(mid)},
                                          /*allowTone=*/false)) {
                continue;
            }
            for (int tone = 33; tone <= 126; ++tone) {
                char toneChar = static_cast<char>(tone);
                if (!ari_ime::isToneKey(toneChar)) {
                    continue;
                }
                std::string keys{lead, static_cast<char>(mid), toneChar};
                if (ari_ime::isValidSyllable(keys, /*allowTone=*/true)) {
                    out = {layout, keys};
                    return true;
                }
            }
        }
    }
    return false;
}

void check_invariants(Sim &s, const char *label) {
    std::string preedit = s.preedit();
    check(valid_utf8(preedit), label);
    int chars = utf8_count(preedit);

    int caret = s.b.caretChar();
    check(caret == -1 || (caret >= 0 && caret <= chars), label);

    int selection = s.b.selectionChar();
    check(selection == -1 || (selection >= 0 && selection < chars), label);

    auto cands = s.cand();
    for (const std::string &cand : cands) {
        check(valid_utf8(cand), label);
    }
    int page = s.b.candidatePage();
    int pages = s.b.candidatePageCount();
    int highlight = s.b.highlight();
    if (cands.empty()) {
        check(page == 0 && pages == 0 && highlight == -1, label);
    } else {
        check(page >= 1 && page <= pages, label);
        check(static_cast<int>(cands.size()) <= 9, label);
        check(highlight >= 0 && highlight < static_cast<int>(cands.size()),
              label);
    }
}

void move_caret_to(Sim &s, int charIndex) {
    s.key(FcitxKey_Home);
    for (int i = 0; i < charIndex; ++i) {
        s.key(FcitxKey_Right);
    }
}

// ---------------------------------------------------------------------------

void test_typing() {
    auto pe = [](const std::string &keys) {
        Sim s;
        s.type(keys);
        return s.preedit();
    };
    check_eq(pe("su3"), "你", "type su3");
    // The clean local dictionary's contextual conversion for ㄋㄧˇㄏㄠˇ.
    check_eq(pe("su3cl3"), "你好", "type su3cl3");
    check_eq(pe("ji3"), "我", "type ji3");
    check_eq(pe("hello"), "hello", "type hello (english)");
    check_eq(pe("apple"), "apple", "type apple (english)");
    check_eq(pe("su3hello"), "你hello", "type su3hello (mixed)");
    // Chinese → English → Chinese: the second syllable must freeze the earlier
    // run + English into cells and start a fresh chewing run (integrateSyllable's
    // non-empty englishBuf_ branch), not silently reset the earlier run.
    check_eq(pe("su3helloji3"), "你hello我", "type su3helloji3 (中英中)");
    check_eq(pe("s3u"), "你", "type s3u (out of order)");
    check_eq(pe("su"), "su", "type su (no tone stays raw)");
    check_eq(pe("aceru/6aj4"), "acer螢幕", "type aceru/6aj4 (peel)");
    const std::pair<const char *, const char *> mixedSuffixes[] = {
        {"linuxy04", "linux"},   // initial + final + tone
        {"ubuntuji3", "ubuntu"}, // medial + final + tone
        {"kernelh04", "kernel"}, // another lowercase initial boundary
    };
    for (const auto &[input, englishPrefix] : mixedSuffixes) {
        const std::string actual = pe(input);
        check(actual.rfind(englishPrefix, 0) == 0 &&
                  contains_han_character(actual),
              (std::string("English prefix peels a valid Chinese suffix: ") +
               input)
                  .c_str());
    }
    check_eq(pe("su3g4"), "你是", "type su3g4 (phrasing)");
    // Bopomofo has no case: uppercase 注音 keys (Shift / Caps Lock) convert the
    // same as lowercase. But uppercase that can't form Chinese stays English,
    // preserving the original case (brand names, acronyms).
    check_eq(pe("SU3"), "你", "type SU3 uppercase -> 你");
    check_eq(pe("Su3CL3"), "你好", "type mixed-case 注音 -> 你好");
    check_eq(pe("Acer"), "Acer", "uppercase-led English keeps its case");
    check_eq(pe("API"), "API", "all-caps acronym stays English");
}

void test_input_error_correction() {
    Sim duplicate;
    duplicate.type("hhk4g4");
    check_eq(duplicate.preedit(), "測試",
             "duplicate leading key is filtered when the suffix converts");

    Sim invalidPrefix;
    invalidPrefix.type("ghk4g4");
    check_eq(invalidPrefix.preedit(), "測試",
             "invalid leading key is trimmed when the suffix converts");

    Sim duplicateFinal;
    duplicateFinal.type("hkk4");
    const std::string duplicateFinalExpected =
        direct_conversion(ari_ime::KeyboardLayout::Default, "hk4");
    check(!duplicateFinalExpected.empty(),
          "canonical suffix converts before duplicate-final comparison");
    check_eq(duplicateFinal.preedit(), duplicateFinalExpected,
             "duplicate final key is filtered when the suffix converts");

    Sim duplicateMedial;
    duplicateMedial.type("suu3");
    check_eq(duplicateMedial.preedit(), "你",
             "duplicate medial key is filtered when the suffix converts");

    Sim incompletePrefix;
    incompletePrefix.type("sucl3");
    check_eq(incompletePrefix.preedit(), "su好",
             "an incomplete non-duplicate prefix is not discarded");

    Sim normalEnglish;
    normalEnglish.type("aceru/6");
    check(normalEnglish.preedit().rfind("acer", 0) == 0 &&
              contains_han_character(normalEnglish.preedit()),
          "ordinary English prefix remains intact during suffix peeling");

    Sim uppercase;
    uppercase.type("HHK4G4");
    check_eq(uppercase.preedit(), "測試",
             "uppercase duplicate and invalid prefixes are corrected");

    Sim shifted;
    auto shiftedKey = [&shifted](fcitx::KeySym sym) {
        return shifted.press(
            fcitx::Key(sym, fcitx::KeyStates{fcitx::KeyState::Shift}));
    };
    shiftedKey(FcitxKey_H);
    shiftedKey(FcitxKey_H);
    shiftedKey(FcitxKey_K);
    shiftedKey(FcitxKey_4);
    shiftedKey(FcitxKey_G);
    shiftedKey(FcitxKey_4);
    check_eq(shifted.preedit(), "測試",
             "shifted physical keys keep typo correction working");

    Sim shiftedSymbol;
    auto shiftedSymbolKey = [&shiftedSymbol](fcitx::KeySym sym) {
        return shiftedSymbol.press(
            fcitx::Key(sym, fcitx::KeyStates{fcitx::KeyState::Shift}));
    };
    shiftedSymbolKey(FcitxKey_H);
    shiftedSymbolKey(FcitxKey_H);
    shiftedSymbolKey(FcitxKey_K);
    shiftedSymbolKey(FcitxKey_dollar);
    shiftedSymbolKey(FcitxKey_G);
    shiftedSymbolKey(FcitxKey_dollar);
    check_eq(shiftedSymbol.preedit(), "測試",
             "shifted symbols keep typo correction working");

    Sim issueNotation;
    issueNotation.type("HHK$G$");
    check_eq(issueNotation.preedit(), "測試",
             "issue notation with shifted tone symbols is corrected");

    Sim invalidIssueNotation;
    invalidIssueNotation.type("GHK$G$");
    check_eq(invalidIssueNotation.preedit(), "測試",
             "issue notation with an invalid prefix is corrected");

    Sim literalDollar;
    literalDollar.type("price");
    literalDollar.press(fcitx::Key(
        FcitxKey_dollar, fcitx::KeyStates{fcitx::KeyState::Shift}));
    check_eq(literalDollar.preedit(), "price$",
             "shifted punctuation stays literal without a Chinese suffix");
}

void test_tone1_space_uses_conversion_result() {
    for (char letter : {'a', 'b'}) {
        Sim singleLetter;
        singleLetter.key(letter);
        singleLetter.key(FcitxKey_space);
        check_eq(singleLetter.preedit(), std::string(1, letter) + " ",
                 "single English letter stays literal on space");
    }

    // Space must use the same result-based decision for out-of-order bodies as
    // it does for a single key. A broad ASCII-word guard would incorrectly
    // turn valid combinations such as these into literal English + Space.
    for (const std::string &keys : {std::string("ls"), std::string("ia"),
                                    std::string("jco")}) {
        Sim outOfOrder;
        outOfOrder.type(keys);
        outOfOrder.key(FcitxKey_space);
        const std::string expected =
            direct_tone1_conversion(ari_ime::KeyboardLayout::Default, keys);
        check(!expected.empty(), "out-of-order tone-one probe yields Han output");
        check_eq(outOfOrder.preedit(), expected.empty() ? keys + " " : expected,
                 ("out-of-order tone-one converts " + keys).c_str());
    }

    Sim canonical;
    canonical.type("sl"); // standard layout: ㄋㄠ in canonical slot order
    canonical.key(FcitxKey_space);
    check(canonical.preedit() != "sl ",
          "canonical tone-1 syllable still converts on space");

    Sim singleVowel;
    singleVowel.key('u'); // default layout: ㄧ
    singleVowel.key(FcitxKey_space);
    check_eq(singleVowel.preedit(), "一",
             "single-key medial still converts under tone one");
    singleVowel.type("-4"); // default layout: ㄦˋ
    check_eq(singleVowel.preedit(), "一二",
             "single-key tone-one syllable can precede another numeral");

    // Keep single-key ㄗ result-based rather than hard-coding one homophone.
    // A clean dictionary may display 姿 first, but 資 must remain available
    // through the normal candidate window. The bare key must remain literal
    // until Space, because y can also begin a longer syllable.
    Sim bareY;
    bareY.key('y');
    check_eq(bareY.preedit(), "y", "bare y remains available for a longer syllable");

    Sim zi;
    zi.key('y');
    zi.key(FcitxKey_space);
    check(contains_han_character(zi.preedit()),
          "single-key ㄗ converts from the actual tone-one result");
    zi.key(FcitxKey_Down);
    check(find_visible_candidate(zi.cand(), "資") >= 0,
          "single-key ㄗ keeps 資 in the candidate list");

    // `5` is another one-key tone-one case reported by users. It must follow
    // the same actual-result rule instead of being treated as English + Space.
    Sim five;
    five.key('5');
    five.key(FcitxKey_space);
    check(contains_han_character(five.preedit()),
          "single-key 5 converts from the actual tone-one result");

    const ari_ime::KeyboardLayout layouts[] = {
        ari_ime::KeyboardLayout::Default,
        ari_ime::KeyboardLayout::Eten,
        ari_ime::KeyboardLayout::Hsu,
        ari_ime::KeyboardLayout::Ibm,
        ari_ime::KeyboardLayout::GinYieh,
        ari_ime::KeyboardLayout::Dvorak,
        ari_ime::KeyboardLayout::Carpalx,
        ari_ime::KeyboardLayout::ColemakDhAnsi,
        ari_ime::KeyboardLayout::ColemakDhOrth,
        ari_ime::KeyboardLayout::Workman,
        ari_ime::KeyboardLayout::Colemak,
    };
    for (const auto layout : layouts) {
        if (!ari_ime::keyboardLayoutAvailable(layout)) {
            continue;
        }
        ari_ime::setCurrentKeyboardLayout(layout);
        for (int value = 33; value <= 126; ++value) {
            const char key = static_cast<char>(value);
            if (ari_ime::zhuyinSlot(key) < 0 || ari_ime::isToneKey(key)) {
                continue;
            }
            const std::string expected =
                direct_tone1_conversion(layout, std::string(1, key));
            Sim actual;
            actual.b.setKeyboardLayout(layout);
            actual.key(key);
            actual.key(FcitxKey_space);
            const std::string literal = std::string(1, key) + " ";
            const std::string label =
                std::string(ari_ime::keyboardLayoutName(layout)) +
                " single-key tone-one decision for " + key;
            check_eq(actual.preedit(), expected.empty() ? literal : expected,
                     label.c_str());
        }
    }
    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

void test_common_mixed_literals() {
    auto pe = [](const std::string &keys) {
        Sim s;
        s.type(keys);
        return s.preedit();
    };

    check_eq(pe("kai@example.com"), "kai@example.com",
             "email address stays literal");
    check_eq(pe("https://ari-ime.test/v1.0.0"),
             "https://ari-ime.test/v1.0.0",
             "URL with version path stays literal");
    check_eq(pe("Ari-IME-1.0.0"), "Ari-IME-1.0.0",
             "hyphenated version string stays literal");
    check_eq(pe("README.md"), "README.md", "filename stays literal");
    check_eq(pe("APIji3"), "API我", "acronym followed by zhuyin converts");
    check_eq(pe("HTTPsu3"), "HTTP你", "uppercase acronym followed by zhuyin converts");

    if (ari_ime::isValidSyllable(".3-3", /*allowTone=*/true)) {
        check_eq(pe("https://ari-ime.test/.3-3"),
                 "https://ari-ime.test/.3-3",
                 "URL suffix that resembles symbol-led zhuyin stays literal");
        check_eq(pe("README.3-3"), "README.3-3",
                 "filename suffix that resembles symbol-led zhuyin stays literal");
        check_eq(pe("v1.0.0.3-3"), "v1.0.0.3-3",
                 "version-like suffix that resembles symbol-led zhuyin stays literal");
        check(pe("acer.3-3") != "acer.3-3",
              "plain English word tail can still peel a symbol-led zhuyin suffix");
    }
}

void test_local_context_prediction_examples() {
    // A punctuation boundary must not trap following Zhuyin in the English
    // token. Default-layout keys: hk4 = 測, g4 = 試.
    Sim punctuation;
    punctuation.key('(');
    punctuation.type("hk4g4");
    check_eq(punctuation.preedit(), "(測試",
             "opening parenthesis keeps following Zhuyin convertible");

    // Keep the whole Chinese run in libchewing so its local phrase model can
    // rank homophones using context instead of choosing each glyph alone.
    // su3=你, u/ + Space=應, e9 + Space=該, g4=試.
    Sim phrase;
    phrase.type("su3u/");
    phrase.key(FcitxKey_space);
    phrase.type("e9");
    phrase.key(FcitxKey_space);
    phrase.type("g4g4");
    // What Ari owns is that the run reaches libchewing whole; which homophone
    // wins (試試 vs 是是 for ㄕˋㄕˋ) is libchewing's contextual ranking and
    // differs between releases. So compare against the same keys fed to
    // libchewing as one run: a Buffer that converted each syllable in isolation
    // would diverge from that wherever context actually changes the outcome.
    const std::string wantPhrase =
        direct_run_conversion(ari_ime::KeyboardLayout::Default, "su3u/ e9 g4g4");
    check(utf8_count(phrase.preedit()) == 5,
          "the whole phrase converts to five characters");
    check(contains_han_character(phrase.preedit()) &&
              phrase.preedit().find_first_of("su3u/e9g4") == std::string::npos,
          "no raw keys leak out of the converted phrase");
    if (!wantPhrase.empty()) {
        check_eq(phrase.preedit(), wantPhrase,
                 "the phrase run is handed to libchewing whole, not per syllable");
    }

    // The same ㄉㄜ˙ reading should follow the surrounding phrase rather than a
    // global one-character preference. ji3=我, 2k7=的/得, ql3=跑,
    // dj94=快 in the default layout.
    Sim possessive;
    possessive.type("ji32k7");
    check_eq(possessive.preedit(), "我的",
             "local context chooses possessive 的");

    Sim complement;
    complement.type("ql32k7dj94");
    check_eq(complement.preedit(), "跑得快",
             "local context chooses complement 得");
}

void test_eten_typing() {
    if (!ari_ime::keyboardLayoutAvailable(ari_ime::KeyboardLayout::Eten)) {
        return;
    }
    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Eten);

    Sim s;
    s.b.setKeyboardLayout(ari_ime::KeyboardLayout::Eten);
    s.type("ne3"); // 倚天: ㄋㄧˇ
    check_eq(s.preedit(), "你", "Eten type ne3 -> 你");

    Sim o;
    o.b.setKeyboardLayout(ari_ime::KeyboardLayout::Eten);
    o.type("n3e");
    check_eq(o.preedit(), "你", "Eten accepts out-of-order n3e");

    Sim phrase;
    phrase.b.setKeyboardLayout(ari_ime::KeyboardLayout::Eten);
    phrase.type("ne3hz3");
    check_eq(phrase.preedit(), "你好", "Eten type ne3hz3 -> 你好");

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

void test_hsu_typing() {
    if (!ari_ime::keyboardLayoutAvailable(ari_ime::KeyboardLayout::Hsu)) {
        return;
    }
    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Hsu);

    Sim s;
    s.b.setKeyboardLayout(ari_ime::KeyboardLayout::Hsu);
    s.type("nef"); // 許氏: ㄋㄧˇ
    check_eq(s.preedit(), "你", "Hsu type nef -> 你");

    Sim o;
    o.b.setKeyboardLayout(ari_ime::KeyboardLayout::Hsu);
    o.type("nfe");
    check_eq(o.preedit(), "你", "Hsu accepts out-of-order nfe");

    Sim phrase;
    phrase.b.setKeyboardLayout(ari_ime::KeyboardLayout::Hsu);
    phrase.type("nefhwf");
    check_eq(phrase.preedit(), "你好", "Hsu type nefhwf -> 你好");

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

void test_additional_layout_typing() {
    struct Case {
        ari_ime::KeyboardLayout layout;
        const char *name;
        const char *ni;
        const char *hao;
    };
    const Case cases[] = {
        {ari_ime::KeyboardLayout::Ibm, "IBM", "7a,", "-;,"},
        {ari_ime::KeyboardLayout::GinYieh, "精業", "d-a", "vla"},
        {ari_ime::KeyboardLayout::Dvorak, "Dvorak", "og3", "jn3"},
        {ari_ime::KeyboardLayout::Carpalx, "Carpalx", "su3", "cl3"},
        {ari_ime::KeyboardLayout::ColemakDhAnsi, "Colemak-DH ANSI", "rl3",
         "di3"},
        {ari_ime::KeyboardLayout::ColemakDhOrth, "Colemak-DH Ortholinear",
         "rl3", "ci3"},
        {ari_ime::KeyboardLayout::Workman, "Workman", "sf3", "mo3"},
        {ari_ime::KeyboardLayout::Colemak, "Colemak", "rl3", "ci3"},
    };

    for (const auto &c : cases) {
        if (!ari_ime::keyboardLayoutAvailable(c.layout)) {
            continue;
        }
        ari_ime::setCurrentKeyboardLayout(c.layout);

        // Expect whatever libchewing makes of the same keys on this layout,
        // not a hardcoded character: its keyboard tables differ between
        // releases, so 精業 `vla` is 好 on one distribution and 吼 on another.
        // What Ari owns — and what this checks — is that routing the keys
        // through the Buffer lands on the same result as feeding libchewing
        // directly.
        Sim single;
        single.b.setKeyboardLayout(c.layout);
        single.type(c.ni);
        const std::string wantNi = direct_conversion(c.layout, c.ni);
        if (!wantNi.empty()) {
            std::string label = std::string(c.name) + " types one character";
            check_eq(single.preedit(), wantNi, label.c_str());
            std::string countLabel =
                std::string(c.name) + " single stays one character";
            check(utf8_count(single.preedit()) == 1, countLabel.c_str());
        }

        Sim phrase;
        phrase.b.setKeyboardLayout(c.layout);
        phrase.type(std::string(c.ni) + c.hao);
        const std::string wantPhrase =
            direct_conversion(c.layout, std::string(c.ni) + c.hao);
        if (!wantPhrase.empty()) {
            std::string label = std::string(c.name) + " types two characters";
            check_eq(phrase.preedit(), wantPhrase, label.c_str());
            std::string countLabel =
                std::string(c.name) + " phrase stays two characters";
            check(utf8_count(phrase.preedit()) == 2, countLabel.c_str());
        }
    }

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

void test_layout_switch_resets_preedit() {
    if (!ari_ime::keyboardLayoutAvailable(ari_ime::KeyboardLayout::Eten)) {
        ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
        return;
    }
    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);

    Sim s;
    s.type("su3");
    check_eq(s.preedit(), "你", "layout switch setup has preedit");
    check(!s.b.setKeyboardLayout(ari_ime::KeyboardLayout::Default),
          "same layout switch reports unchanged");
    check_eq(s.preedit(), "你", "same layout keeps preedit");

    check(s.b.setKeyboardLayout(ari_ime::KeyboardLayout::Eten),
          "different layout switch reports changed");
    check_eq(s.preedit(), "", "layout switch clears preedit");
    s.type("ne3");
    check_eq(s.preedit(), "你", "typing resumes with switched layout");

    s.b.setKeyboardLayout(ari_ime::KeyboardLayout::Default);
    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

void test_keypad_literal() {
    Sim s;
    s.key(FcitxKey_KP_1);
    s.key(FcitxKey_KP_2);
    s.key(FcitxKey_KP_3);
    check_eq(s.preedit(), "123", "keypad 123 literal");

    Sim s2;
    s2.type("su");
    s2.key(FcitxKey_KP_3); // keypad 3 is literal, must NOT tone su -> 你
    check_eq(s2.preedit(), "su3", "su + keypad3 stays literal");
}

void test_keypad_navigation() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3");       // 你好
    s.key(FcitxKey_KP_Home); // NumLock off keypad Home
    s.type("1j4");          // ㄅㄨˋ, default homophone depends on libchewing
    check_eq(s.preedit(), bu + "你好", "keypad Home moves caret to front");

    Sim begin;
    begin.type("su3cl3");       // 你好
    begin.key(FcitxKey_KP_Begin); // NumLock off keypad 5 / Begin
    begin.type("1j4");          // ㄅㄨˋ
    check_eq(begin.preedit(), bu + "你好",
             "keypad Begin moves caret to front");

    Sim e;
    e.type("su3cl3");       // 你好
    e.key(FcitxKey_KP_Home);
    e.key(FcitxKey_KP_End);
    e.type("1j4");          // ㄅㄨˋ
    check_eq(e.preedit(), "你好" + bu, "keypad End moves caret to end");

    Sim d;
    d.type("su3cl3");       // 你好
    d.key(FcitxKey_KP_Left); // caret between 你 and 好
    d.key(FcitxKey_KP_Delete);
    check_eq(d.preedit(), "你", "keypad Delete removes char to the right");

    Sim p;
    p.type("su3");
    p.key(FcitxKey_KP_Down);
    check(!p.cand().empty(), "keypad Down opens candidates");
    int firstPage = p.b.candidatePage();
    p.key(FcitxKey_KP_Next);
    check(p.b.candidatePage() == firstPage + 1,
          "keypad PageDown advances candidate page");
    p.key(FcitxKey_KP_Prior);
    check(p.b.candidatePage() == firstPage,
          "keypad PageUp rewinds candidate page");

    Sim space;
    space.type("hello");
    space.key(FcitxKey_KP_Space);
    check_eq(space.preedit(), "hello ", "keypad Space inserts literal separator");
}

void test_enter_commit() {
    Sim s;
    s.type("su3hello");
    s.key(FcitxKey_Return);
    check_eq(s.committed, "你hello", "enter commits 你hello");
    check_eq(s.preedit(), "", "preedit cleared after commit");

    Sim e;
    e.key(FcitxKey_Return); // nothing pending
    check_eq(e.committed, "", "empty enter commits nothing");
}

void test_forced_english_toggle() {
    Sim s;
    s.type("su3");
    KeyResult r = s.press(fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl));
    check(r.handled && r.notifyMode, "Ctrl+Space toggles forced English on");
    check(s.b.isForcedEnglish(), "forced English flag turns on");
    check_eq(s.preedit(), "你", "forced English keeps existing pre-edit");

    s.type("su3");
    check_eq(s.preedit(), "你su3", "forced English keeps zhuyin keys literal");

    r = s.press(fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl));
    check(r.handled && r.notifyMode, "Ctrl+Space toggles forced English off");
    check(!s.b.isForcedEnglish(), "forced English flag turns off");
    s.type("cl3");
    check_eq(s.preedit(), "你su3好", "typing resumes Chinese after forced mode");

    s.key(FcitxKey_Return);
    check_eq(s.committed, "你su3好", "forced mode content commits with pre-edit");
    check_eq(s.preedit(), "", "preedit clears after forced mode commit");

    Sim kp;
    r = kp.press(fcitx::Key(FcitxKey_KP_Space, fcitx::KeyState::Ctrl));
    check(r.handled && r.notifyMode, "Ctrl+keypad Space toggles forced English");
    check(kp.b.isForcedEnglish(), "keypad Space toggles forced English flag");
    kp.type("su3");
    check_eq(kp.preedit(), "su3",
             "typing after Ctrl+keypad Space remains literal");
}

void test_forced_english_persists_across_reset() {
    Sim commit;
    commit.press(fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl));
    commit.type("su3");
    commit.key(FcitxKey_Return);
    check_eq(commit.committed, "su3", "forced English commit stays literal");
    check(commit.b.isForcedEnglish(), "forced English persists after commit");
    commit.type("cl3");
    check_eq(commit.preedit(), "cl3",
             "typing after forced English commit remains literal");

    Sim esc;
    esc.press(fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl));
    esc.type("su3");
    esc.key(FcitxKey_Escape);
    check_eq(esc.preedit(), "", "Escape clears forced English pre-edit");
    check(esc.b.isForcedEnglish(), "forced English persists after Escape clear");
    esc.type("su3");
    check_eq(esc.preedit(), "su3",
             "typing after forced English Escape remains literal");
}

void test_forced_english_caret_editing() {
    Sim s;
    s.press(fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl));
    s.type("acb");
    s.key(FcitxKey_Left);
    s.key(FcitxKey_BackSpace);
    s.type("B");
    check_eq(s.preedit(), "aBb",
             "forced English supports caret movement and mid-string editing");

    s.key(FcitxKey_Up);
    check_eq(s.preedit(), "aBb",
             "forced English caret does not reinterpret text as Zhuyin");
}

void test_backspace() {
    Sim s;
    s.type("su3cl3"); // 你好
    s.key(FcitxKey_BackSpace);
    check_eq(s.preedit(), "你", "backspace deletes 好");

    Sim outOfOrder;
    outOfOrder.type("140");
    check(contains_han_character(outOfOrder.preedit()),
          "out-of-order syllable converts before backspace");
    outOfOrder.key(FcitxKey_BackSpace);
    check_eq(outOfOrder.preedit(), "140",
             "backspace restores out-of-order keys in their typed order");

    Sim toneBeforeFinal;
    toneBeforeFinal.type("240");
    check(contains_han_character(toneBeforeFinal.preedit()),
          "240 converts before backspace");
    toneBeforeFinal.key(FcitxKey_BackSpace);
    check_eq(toneBeforeFinal.preedit(), "240",
             "backspace restores tone-before-final keys in typed order");

    Sim toneBeforeFinalCandidate;
    toneBeforeFinalCandidate.type("240");
    toneBeforeFinalCandidate.key(FcitxKey_Down);
    toneBeforeFinalCandidate.key(FcitxKey_Up);
    check(!toneBeforeFinalCandidate.cand().empty() &&
              toneBeforeFinalCandidate.cand().back() == "原始鍵 240",
          "raw-key candidate keeps 240 in typed order");
    toneBeforeFinalCandidate.key(FcitxKey_Return);
    check_eq(toneBeforeFinalCandidate.preedit(), "240",
             "raw-key revert restores 240 in typed order");
}

// Caret model: ←/→ move a caret between characters; ↓ opens the candidate window
// for the character the caret points AT — the one to its RIGHT. To re-pick 你
// the caret must sit before it (two Lefts to the front).
void test_phrase_priority() {
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left); // caret between 你 and 好
    s.key(FcitxKey_Left); // caret before 你
    s.key(FcitxKey_Down); // open candidates for 你 (the char to the right)
    auto c = s.cand();
    check(!c.empty() && c[0] == "你好", "phrase 你好 listed first");
    int singleIndex = find_visible_candidate(c, "你");
    check(singleIndex > 0, "single 你 remains available after phrase candidates");
    int rawIndex = find_visible_candidate(c, "原始鍵 su3");
    check(rawIndex < 0 || rawIndex > singleIndex,
          "raw-key fallback stays behind phrase choices in Chinese context");
    check(rawIndex < 0 || rawIndex == static_cast<int>(c.size()) - 1,
          "raw-key fallback stays at the end of the visible candidate page");
}

void test_forward_phrase_recommendation() {
    Sim s;
    s.type("hk4g4"); // 測試
    s.key(FcitxKey_Down); // open candidates from the final character 試
    const auto candidates = s.cand();
    check(!candidates.empty(), "forward-only candidates open at the target");
    check(find_visible_candidate(candidates, "試") >= 0,
          "target character remains available in forward-only candidates");
    check(find_visible_candidate(candidates, "測試") < 0,
          "candidates do not inspect the phrase before the target");

    Sim middle;
    middle.type("su3cl3"); // 你好
    middle.key(FcitxKey_Left); // caret before 好
    middle.key(FcitxKey_Down);
    check(find_visible_candidate(middle.cand(), "你好") < 0,
          "middle-character candidates do not include a preceding phrase");
}

void test_live_candidate_preview() {
    Sim s;
    s.type("hk4g4"); // 測試
    const auto recommendations = s.preview();
    check(!recommendations.empty(), "completed Chinese text has a live preview");
    check_eq(recommendations.front(), "測試",
             "live preview starts with the visible contextual result");

    // The preview is display-only: a following digit is still a new 注音 key,
    // not an accidental candidate selection.
    s.type("su3");
    check_eq(s.preedit(), "測試你",
             "preview does not steal digits from mixed Chinese input");
    check(s.preview().empty() == false,
          "preview returns after the next Chinese syllable completes");

    s.key(FcitxKey_Down);
    check(!s.cand().empty(),
          "Down turns the live preview into the interactive candidate picker");
}

// The selection window keeps libchewing's contextual live result first instead
// of replacing it with an unrelated static candidate-list order.
void test_live_matches_top_candidate() {
    Sim s;
    s.type("su3cl3");
    const std::string live = s.preedit();
    s.key(FcitxKey_Left); // caret between 你 and 好
    s.key(FcitxKey_Left); // caret before 你
    s.key(FcitxKey_Down); // open candidates for 你
    auto c = s.cand();
    check(!c.empty() && c[0] == live, "selection top candidate matches live");
}

void test_reconversion_core() {
    Sim s;
    const KeyResult result = s.b.beginReconversion("測試");
    check(result.handled, "short Chinese text can enter reconversion");
    check(s.b.isPicking(), "reconversion opens the native candidate state");
    check_eq(s.preedit(), "測試", "reconversion preserves the selected text");
    check(find_visible_candidate(s.cand(), "測試") >= 0,
          "reconversion keeps the original phrase available");

    const int alternative = find_visible_candidate(s.cand(), "策士");
    check(alternative >= 0,
          "reconversion exposes a phrase alternative for the selected text");
    if (alternative >= 0) {
        check(s.b.selectCandidate(alternative).handled,
              "reconversion phrase alternative can be selected");
        check_eq(s.preedit(), "策士",
                 "reconversion replaces the selected text with the choice");
    }

    Sim repeated;
    check(repeated.b.beginReconversion("你你").handled,
          "reconversion handles repeated target characters");
    check_eq(repeated.preedit(), "你你",
             "reconversion preserves repeated target characters");

    Sim mixed;
    check(!mixed.b.beginReconversion("測試!").handled,
          "reconversion rejects mixed text it cannot reverse safely");
    check_eq(mixed.preedit(), "", "reconversion rejection leaves buffer empty");

    std::string longText;
    for (int i = 0; i < ari_ime::kMaxCompositionChars + 1; ++i) {
        longText += "你";
    }
    Sim longSelection;
    check(!longSelection.b.beginReconversion(longText).handled,
          "reconversion rejects text beyond the short-selection limit");
}

void test_phrase_pick() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left); // caret between 你 and 好
    s.key(FcitxKey_Left); // caret before 你
    s.key(FcitxKey_Down); // open candidates for 你
    int phraseIndex = find_visible_candidate(s.cand(), "妳好");
    check(phraseIndex >= 0, "visible candidates include 妳好");
    KeyResult picked = s.b.selectCandidate(phraseIndex);
    check(picked.handled, "phrase pick selects visible phrase candidate");
    check_eq(s.preedit(), "妳好", "phrase pick rewrites both cells");
    s.type("1j4");
    check_eq(s.preedit(), "妳好" + bu,
             "typing after end-of-line phrase pick appends at tail");
}

// Correcting a character is a mid-string edit: the caret belongs on the
// character right after the one that was fixed, so the next keystroke continues
// there. It used to snap to the end of the pre-edit no matter where the user
// was working.
void test_pick_leaves_caret_after_correction() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl31j4"); // 你好 + one more character
    const std::string composed = s.preedit();
    check(utf8_count(composed) == 3,
          "caret-after-pick setup composes three characters");

    s.key(FcitxKey_Home); // caret mode, caret before the first character
    s.key(FcitxKey_Down); // open candidates for the first character
    check(s.b.isPicking(), "caret-after-pick setup opens the candidate window");
    const int niIndex = find_visible_candidate(s.cand(), "妳");
    check(niIndex >= 0, "visible candidates include 妳 for the caret test");
    KeyResult picked = s.b.selectCandidate(niIndex);
    check(picked.handled, "caret test picks 妳 directly");

    check(!s.b.isPicking(), "a completed pick closes the candidate window");
    check(s.b.isEditing(), "a completed pick stays in caret editing");
    check(s.b.caretChar() == 1,
          "the caret lands on the character after the corrected one");

    // Typing resumes at the caret, not at the end of the line.
    s.type("1j4");
    check_eq(s.preedit(), "妳" + bu + utf8_char_at(composed, 1) +
                              utf8_char_at(composed, 2),
             "typing after a mid-string pick inserts at the caret");

    // A phrase pick spans several cells; the caret clears the whole phrase.
    Sim phrase;
    phrase.type("su3cl31j4");
    phrase.key(FcitxKey_Home);
    phrase.key(FcitxKey_Down);
    const int phraseIndex = find_visible_candidate(phrase.cand(), "妳好");
    if (phraseIndex >= 0) {
        check(phrase.b.selectCandidate(phraseIndex).handled,
              "caret test picks the 妳好 phrase");
        check(phrase.b.caretChar() == 2,
              "the caret lands after the whole phrase a pick rewrote");
    }

    // Punctuation cells use the same picker and must behave the same way.
    Sim punct;
    punct.key('[');  // a literal punctuation cell
    punct.type("su3cl3");
    punct.key(FcitxKey_Home);
    punct.key(FcitxKey_Down); // candidates for the punctuation cell
    const auto punctCands = punct.cand();
    check(!punctCands.empty(), "punctuation cell opens its own candidates");
    int variant = -1;
    for (int i = 0; i < static_cast<int>(punctCands.size()); ++i) {
        if (punctCands[i] != "[") {
            variant = i;
            break;
        }
    }
    check(variant >= 0, "punctuation picker offers another variant");
    check(punct.b.selectCandidate(variant).handled,
          "caret test picks a punctuation variant");
    check_eq(utf8_char_at(punct.preedit(), 0), punctCands[variant],
             "punctuation pick rewrites the focused cell");
    check(punct.b.caretChar() == 1,
          "the caret lands after a corrected punctuation cell too");

    // Fixing the final character still leaves the caret at the end, so the
    // common "correct the last character, keep typing" flow is unchanged.
    Sim last;
    last.type("su3cl3");
    last.key(FcitxKey_Down); // caret at the end -> candidates for the last cell
    const int haoIndex = find_visible_candidate(last.cand(), "郝");
    check(haoIndex >= 0, "visible candidates include 郝 for the caret test");
    check(last.b.selectCandidate(haoIndex).handled, "caret test picks 郝");
    check(last.b.caretChar() == 2,
          "correcting the last character leaves the caret at the end");
    last.type("1j4");
    check_eq(last.preedit(), "你郝" + bu,
             "typing after correcting the last character still appends");
}

void test_candidate_direct_selection() {
    const std::string bu = bu4_default();

    Sim empty;
    KeyResult r = empty.b.selectCandidate(0);
    check(!r.handled, "direct candidate selection without window passes through");

    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left); // caret between 你 and 好
    s.key(FcitxKey_Left); // caret before 你
    s.key(FcitxKey_Down); // open candidates for 你
    int phraseIndex = find_visible_candidate(s.cand(), "妳好");
    check(phraseIndex >= 0, "visible candidates include 妳好");
    r = s.b.selectCandidate(phraseIndex);
    check(r.handled, "direct candidate selection handles visible candidate");
    check_eq(s.preedit(), "妳好",
             "direct candidate selection rewrites phrase like number key");

    Sim single;
    single.type("su3cl3");
    single.key(FcitxKey_Left); // caret between 你 and 好
    single.key(FcitxKey_Left); // caret before 你
    single.key(FcitxKey_Down); // open candidates for 你
    int niIndex = find_visible_candidate(single.cand(), "妳");
    check(niIndex >= 0, "visible candidates include 妳");
    r = single.b.selectCandidate(niIndex);
    check(r.handled, "direct candidate selection handles single candidate");
    check_eq(single.preedit(), "妳好",
             "direct single candidate rewrites focused cell");
    check(single.b.isEditing() && !single.b.isPicking() &&
              single.b.selectionChar() == -1,
          "direct single candidate closes the window but stays in caret mode");
    check(single.b.caretChar() == 1,
          "direct single candidate parks the caret after the fixed character");
    single.type("1j4");
    check_eq(single.preedit(), "妳" + bu + "好",
             "typing after direct pick continues at the corrected position");

    Sim stale;
    stale.type("su3");
    stale.key(FcitxKey_Down);
    r = stale.b.selectCandidate(99);
    check(r.handled, "stale direct candidate index is absorbed");
    check(!stale.cand().empty(),
          "stale direct candidate index keeps candidate window open");
    check_eq(stale.preedit(), "你",
             "stale direct candidate index leaves preedit unchanged");
}

void test_stale_candidate_activation_is_ignored() {
    Sim s;
    s.type("su3");
    s.key(FcitxKey_Down);
    const auto firstPage = s.cand();
    check(!firstPage.empty(), "stale activation setup opens candidates");
    const std::string oldCandidate = firstPage.front();

    s.key(FcitxKey_Page_Down);
    const auto secondPage = s.cand();
    check(!secondPage.empty() && secondPage != firstPage,
          "stale activation setup moves to another page");
    KeyResult stale = s.b.selectCandidate(0, oldCandidate);
    check(stale.handled && !stale.hasCommit,
          "stale candidate activation is absorbed safely");
    check_eq(s.preedit(), "你",
             "stale candidate activation does not rewrite the pre-edit");
    check(s.b.candidatePage() == 2,
          "stale candidate activation keeps the current page open");

    const std::string currentCandidate = secondPage.front();
    KeyResult current = s.b.selectCandidate(0, currentCandidate);
    check(current.handled, "current candidate activation still selects normally");
}

void test_pin_earlier_pick() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left);  // caret between 你 and 好
    s.key(FcitxKey_Left);  // caret before 你
    s.key(FcitxKey_Down);  // open candidates for 你 (hl0 你好)
    int niPinnedIndex = find_visible_candidate(s.cand(), "妳");
    check(niPinnedIndex >= 0, "visible candidates include 妳 for pinning test");
    KeyResult pinned = s.b.selectCandidate(niPinnedIndex);
    check(pinned.handled, "pinning test picks 妳 directly");
    check_eq(s.preedit(), "妳好", "picked 妳 single");
    check(s.b.isEditing() && !s.b.isPicking() && s.b.caretChar() == 1,
          "pick leaves the caret just after the character it fixed");
    // Reopen correction on 好 and fix it to 郝. The earlier 妳 pick must stay locked.
    s.key(FcitxKey_Home);
    s.key(FcitxKey_Right);
    s.key(FcitxKey_Down);
    int haoPinnedIndex = find_visible_candidate(s.cand(), "郝");
    check(haoPinnedIndex >= 0, "visible candidates include 郝 for pinning test");
    KeyResult haoPinned = s.b.selectCandidate(haoPinnedIndex);
    check(haoPinned.handled, "pinning test picks 郝 directly");
    check_eq(s.preedit(), "妳郝", "earlier 妳 stays locked after picking 郝");
    check(s.b.isEditing() && !s.b.isPicking() && s.b.caretChar() == 2,
          "fixing the final character leaves the caret at the end");
    s.type("1j4");
    check_eq(s.preedit(), "妳郝" + bu,
             "typing after reopened correction appends after fixed text");
}

void test_candidate_ranking_prefers_current_choice() {
    Sim s;
    s.type("su3");
    s.key(FcitxKey_Down);
    int niIndex = find_visible_candidate(s.cand(), "妳");
    check(niIndex >= 0, "visible candidates include 妳");
    KeyResult picked = s.b.selectCandidate(niIndex);
    check(picked.handled, "direct pick selects 妳");
    check_eq(s.preedit(), "妳", "explicit pick updates preedit");
    s.key(FcitxKey_Home);
    s.key(FcitxKey_Down);
    auto reopened = s.cand();
    check(!reopened.empty() && reopened[0] == "妳",
          "reopened candidates prefer the current explicit choice");
}

void test_candidate_selection_undo() {
    Sim empty;
    KeyResult r = empty.press(
        fcitx::Key(FcitxKey_z, fcitx::KeyState::Ctrl));
    check(!r.handled, "Ctrl+Z without a candidate choice passes through");

    Sim phrase;
    phrase.type("su3cl3");
    phrase.key(FcitxKey_Home);
    phrase.key(FcitxKey_Down);
    const int phraseIndex = find_visible_candidate(phrase.cand(), "妳好");
    check(phraseIndex >= 0, "selection undo setup includes 妳好");
    phrase.b.selectCandidate(phraseIndex);
    check_eq(phrase.preedit(), "妳好", "selection undo setup picks phrase");
    r = phrase.press(fcitx::Key(FcitxKey_z, fcitx::KeyState::Ctrl));
    check(r.handled, "Ctrl+Z handles a recent candidate choice");
    check_eq(r.notification, "已復原選字", "selection undo reports its action");
    check_eq(phrase.preedit(), "你好", "Ctrl+Z restores text before phrase pick");

    Sim multiple;
    multiple.type("su3cl3");
    multiple.key(FcitxKey_Home);
    multiple.key(FcitxKey_Down);
    const int niIndex = find_visible_candidate(multiple.cand(), "妳");
    check(niIndex >= 0, "multi-level undo setup includes 妳");
    multiple.b.selectCandidate(niIndex);
    multiple.key(FcitxKey_Home);
    multiple.key(FcitxKey_Right);
    multiple.key(FcitxKey_Down);
    const int haoIndex = find_visible_candidate(multiple.cand(), "郝");
    check(haoIndex >= 0, "multi-level undo setup includes 郝");
    multiple.b.selectCandidate(haoIndex);
    check_eq(multiple.preedit(), "妳郝", "multi-level undo applies two choices");
    multiple.press(fcitx::Key(FcitxKey_z, fcitx::KeyState::Ctrl));
    check_eq(multiple.preedit(), "妳好", "first Ctrl+Z restores latest choice");
    multiple.press(fcitx::Key(FcitxKey_z, fcitx::KeyState::Ctrl));
    check_eq(multiple.preedit(), "你好", "second Ctrl+Z restores earlier choice");

    Sim invalidated;
    invalidated.type("su3");
    invalidated.key(FcitxKey_Down);
    const int altIndex = find_visible_candidate(invalidated.cand(), "妳");
    check(altIndex >= 0, "undo invalidation setup includes 妳");
    invalidated.b.selectCandidate(altIndex);
    invalidated.type("cl3");
    r = invalidated.press(fcitx::Key(FcitxKey_z, fcitx::KeyState::Ctrl));
    check(!r.handled, "typing after a choice releases Ctrl+Z to the application");
    check_eq(invalidated.preedit(), "妳好",
             "released application undo does not alter the active preedit");
}

void test_symbol_heavy_context_keeps_chinese_first() {
    SymbolLeadCase openParen;
    if (!find_symbol_lead_case('(', openParen)) {
        ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
        return;
    }

    ari_ime::setCurrentKeyboardLayout(openParen.layout);

    Sim s;
    s.b.setKeyboardLayout(openParen.layout);
    s.key('?');
    s.type(openParen.keys);
    check(s.preedit().size() > openParen.keys.size(),
          "symbol-led zhuyin converts after literal punctuation");
    s.key(FcitxKey_Left); // caret between ? and the converted Chinese cell
    s.key(FcitxKey_Down); // open candidates on the converted cell
    auto cands = s.cand();
    check(!cands.empty(), "symbol-led context opens candidates");
    check(cands[0] != "原始鍵 " + openParen.keys,
          "symbol-heavy context still keeps Chinese candidates ahead of raw keys");
    int rawIndex = find_visible_candidate(cands, "原始鍵 " + openParen.keys);
    check(rawIndex < 0 || rawIndex == static_cast<int>(cands.size()) - 1,
          "raw-key fallback remains available as the last visible choice");

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

// The headline fix: a 注音 syllable whose FIRST key is a number-row key (不 =
// ㄅㄨˋ = "1j4") must insert at the caret, not get eaten as a candidate pick.
void test_insert_chinese_midstring() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3");       // 你好
    s.key(FcitxKey_Left);   // caret between 你 and 好
    s.key(FcitxKey_Left);   // caret before 你 (front)
    s.type("1j4");          // leading digit must NOT pick a candidate
    check_eq(s.preedit(), bu + "你好", "insert digit-led 注音 at the front");
    check(s.b.caretChar() == 1, "caret sits after the inserted char, not at end");
    s.key(FcitxKey_Return);
    check_eq(s.committed, bu + "你好", "commit includes inserted char + tail");

    // Insert in the middle, not just the front.
    Sim m;
    m.type("su3cl3");       // 你好
    m.key(FcitxKey_Left);   // caret between 你 and 好
    m.type("1j4");          // insert ㄅㄨˋ before 好
    check_eq(m.preedit(), "你" + bu + "好", "insert 注音 between 你 and 好");
    check(m.b.caretChar() == 2, "caret sits between inserted char and 好, not at end");
}

// Paste lands at the caret like any other action, and typing continues there.
void test_paste_at_caret() {
    const std::string bu = bu4_default();

    Sim fresh;
    fresh.b.pasteAtCaret("ABC");
    check_eq(fresh.preedit(), "ABC", "paste can start a fresh pre-edit");
    fresh.type("1j4");
    check_eq(fresh.preedit(), "ABC" + bu,
             "typing continues after paste-started pre-edit");

    Sim s;
    s.type("su3cl3");      // 你好
    s.key(FcitxKey_Left);  // caret between 你 and 好
    s.b.pasteAtCaret("ABC");
    check_eq(s.preedit(), "你ABC好", "paste lands at the caret");
    check(s.b.caretChar() == 4, "caret sits right after the pasted text");
    s.type("1j4");         // keep composing at the caret
    check_eq(s.preedit(), "你ABC" + bu + "好", "typing continues at the caret after paste");

    // Pasting while typing at the end folds the live run in first.
    Sim e;
    e.type("su3");         // 你 (live)
    e.b.pasteAtCaret("xy");
    check_eq(e.preedit(), "你xy", "paste at the end appends after the run");

    Sim ws;
    ws.b.pasteAtCaret("alpha\tbeta\n\ngamma\r\ndelta");
    check_eq(ws.preedit(), "alpha beta gamma delta",
             "paste normalizes tabs and newlines to spaces");

    Sim controls;
    controls.b.pasteAtCaret(std::string("alpha") + '\0' + '\x1b' + "beta" +
                            '\x7f' + "gamma");
    check_eq(controls.preedit(), "alpha beta gamma",
             "paste normalizes ASCII controls to spaces");

    Sim unicodeSeparators;
    unicodeSeparators.b.pasteAtCaret(
        std::string("alpha") + "\xc2\xa0" + "beta" + "\xe2\x80\xa8" +
        "gamma" + "\xe2\x80\xa9" + "delta");
    check_eq(unicodeSeparators.preedit(), "alpha beta gamma delta",
             "paste normalizes common Unicode separators to spaces");

    Sim extraUnicodeSeparators;
    extraUnicodeSeparators.b.pasteAtCaret(
        std::string("alpha") + "\xe3\x80\x80" + "beta" + "\xe2\x80\xaf" +
        "gamma");
    check_eq(extraUnicodeSeparators.preedit(), "alpha beta gamma",
             "paste normalizes ideographic and narrow no-break spaces");

    Sim zeroWidth;
    zeroWidth.b.pasteAtCaret(std::string("alpha") + "\xe2\x80\x8b" +
                             "beta" + "\xe2\x81\xa0" + "gamma" +
                             "\xef\xbb\xbf" + "delta");
    check_eq(zeroWidth.preedit(), "alphabetagammadelta",
             "paste removes zero-width format characters");

    Sim onlyZeroWidth;
    onlyZeroWidth.b.pasteAtCaret(std::string("\xe2\x80\x8b") +
                                 "\xe2\x81\xa0" + "\xef\xbb\xbf");
    check_eq(onlyZeroWidth.preedit(), "",
             "paste of only zero-width format characters is empty");
    KeyResult ignoredPasteEsc = onlyZeroWidth.press(FcitxKey_Escape);
    check(!ignoredPasteEsc.handled,
          "paste of only zero-width format characters leaves no IM state");

    Sim onlyWs;
    onlyWs.b.pasteAtCaret("\n\t");
    check_eq(onlyWs.preedit(), " ", "paste of only separators becomes a space");
}

void test_paste_caret_with_multi_codepoint_cells() {
    Sim s;
    s.b.setFullWidthPunct(true);
    s.type("^su3");         // ……你
    s.key(FcitxKey_Left);   // caret between …… and 你
    check(s.b.caretChar() == 2,
          "caret counts the full-width ellipsis as two visible characters");
    s.b.pasteAtCaret("ABC");
    check_eq(s.preedit(), "……ABC你",
             "paste stays at the visible caret after a multi-codepoint cell");
    check(s.b.caretChar() == 5,
          "caret stays right after pasted text in multi-codepoint context");
}

void test_paste_grapheme_editing() {
    // Each of these contains multiple Unicode codepoints but must behave as
    // one editable unit: a ZWJ developer emoji, a variation-selector heart,
    // and a regional-indicator flag.
    Sim emoji;
    emoji.b.pasteAtCaret("A👨‍💻❤️🇹🇼");
    check_eq(emoji.preedit(), "A👨‍💻❤️🇹🇼",
             "paste preserves multi-codepoint grapheme clusters");
    check(emoji.b.caretChar() == 4,
          "caret counts grapheme clusters rather than UTF-8 codepoints");

    emoji.key(FcitxKey_BackSpace);
    check_eq(emoji.preedit(), "A👨‍💻❤️",
             "Backspace removes a whole regional-indicator flag");
    emoji.key(FcitxKey_BackSpace);
    check_eq(emoji.preedit(), "A👨‍💻",
             "Backspace removes a whole variation-selector grapheme");
    emoji.key(FcitxKey_BackSpace);
    check_eq(emoji.preedit(), "A",
             "Backspace removes a whole ZWJ emoji sequence");
}

void test_normal_caret_counts_graphemes() {
    Sim s;
    s.b.setFullWidthPunct(true);
    s.type("^su3"); // ……你; …… is one cell but two displayed graphemes
    s.key(FcitxKey_Home);
    s.key(FcitxKey_Right); // between …… and 你
    s.type("A");
    check_eq(s.preedit(), "……A你", "normal typing keeps multi-grapheme cells");
    check(s.b.caretChar() == 3,
          "normal caret counts graphemes inside multi-codepoint cells");
}

void test_midstring_delete_boundaries() {
    Sim s;
    s.type("su3cl3");        // 你好
    s.key(FcitxKey_Left);    // caret between 你 and 好
    s.key(FcitxKey_Home);    // caret before 你
    s.key('A');              // insert before the parked 你好 tail
    check_eq(s.preedit(), "A你好", "insert literal before parked tail");
    s.key(FcitxKey_Delete);  // delete the char right of the insertion tail
    check_eq(s.preedit(), "A好", "Delete removes first parked tail cell");
    s.key(FcitxKey_BackSpace);
    check_eq(s.preedit(), "好", "Backspace removes inserted head");
    KeyResult r = s.press(FcitxKey_BackSpace);
    check(r.handled, "Backspace at parked-tail front is absorbed");
    check_eq(s.preedit(), "好", "Backspace at parked-tail front is a no-op");

    Sim e;
    e.type("su3");
    r = e.press(FcitxKey_Delete);
    check(r.handled, "Delete at end of active pre-edit is absorbed");
    check_eq(e.preedit(), "你", "Delete at end keeps pre-edit unchanged");

    Sim empty;
    r = empty.press(FcitxKey_Delete);
    check(!r.handled, "Delete with no pre-edit passes through");
}

void test_up_navigates_not_revert() {
    Sim s;
    s.type("ji3");        // 我 (stable top candidate for ㄨㄛˇ)
    s.key(FcitxKey_Down); // enter + open candidates for 我 (hl0)
    s.key(FcitxKey_Down); // hl1
    s.key(FcitxKey_Up);   // back to hl0 — must NOT revert to english
    check_eq(s.preedit(), "我", "Up navigates candidates, no revert");
}

void test_revert_entry() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3");        // 你
    s.key(FcitxKey_Down); // enter + open candidates for 你
    s.key(FcitxKey_Up);   // wrap to the last entry = raw-keys revert
    auto c = s.cand();
    check(!c.empty() && c.back() == "原始鍵 su3",
          "raw-keys candidate is labeled");
    s.key(FcitxKey_Return);
    check_eq(s.preedit(), "su3", "revert entry explodes 你 -> su3");
    s.type("cl3");
    check_eq(s.preedit(), "su3好",
             "typing after tail raw-key revert resumes after exploded keys");

    Sim mid;
    mid.type("su3cl3");       // 你好
    mid.key(FcitxKey_Left);   // caret between 你 and 好
    mid.key(FcitxKey_Left);   // caret before 你
    mid.key(FcitxKey_Down);   // candidate window for 你
    mid.key(FcitxKey_Up);     // raw-keys revert candidate
    mid.key(FcitxKey_Return);
    check_eq(mid.preedit(), "su3好",
             "mid-string raw-key revert preserves following cells");
    mid.type("1j4");
    check_eq(mid.preedit(), "su3" + bu + "好",
             "typing after mid-string raw-key revert resumes before next cell");

    Sim uppercaseI;
    uppercaseI.press(fcitx::Key(
        FcitxKey_i, fcitx::KeyStates{fcitx::KeyState::Shift}));
    uppercaseI.key(FcitxKey_space); // I/i -> 喔 with 一聲
    check_eq(uppercaseI.preedit(), "喔",
             "shifted i converts while retaining its original key case");
    uppercaseI.key(FcitxKey_Down);
    const int uppercaseRaw =
        find_visible_candidate(uppercaseI.cand(), "原始鍵 I");
    check(uppercaseRaw >= 0,
          "raw-key recovery keeps uppercase input state");
    uppercaseI.b.selectCandidate(uppercaseRaw);
    check_eq(uppercaseI.preedit(), "I",
             "raw-key recovery restores the originally typed uppercase key");

    // Out-of-order typing: ㄚ before ㄅ still converts to 拔, but reverting must
    // give back the keys in the order typed (816), not the canonical 186.
    Sim outOfOrder;
    outOfOrder.type("8168");
    check_eq(outOfOrder.preedit(), "拔8",
             "out-of-order 816 converts to 拔 before a pending 8");
    outOfOrder.key(FcitxKey_Left);
    outOfOrder.key(FcitxKey_Left); // caret before 拔
    outOfOrder.key(FcitxKey_Down);
    outOfOrder.key(FcitxKey_Up);   // raw-keys revert candidate
    check(!outOfOrder.cand().empty() &&
              outOfOrder.cand().back() == "原始鍵 816",
          "raw-keys candidate shows the typed order");
    outOfOrder.key(FcitxKey_Return);
    check_eq(outOfOrder.preedit(), "8168",
             "out-of-order raw-key revert restores 816, not 186");
}

// CandidateArrowKeys picks who owns ←/→ while candidates are open, and
// CaretAfterPick picks where the caret lands once one is chosen. Both defaults
// are asserted by the surrounding tests; these cover the non-default settings.
void test_candidate_arrow_key_and_caret_options() {
    const std::string bu = bu4_default();

    // ChangePage: ←/→ turn pages instead of walking to the next character.
    Sim page;
    check(page.b.setCandidateArrowKeys(ari_ime::CandidateArrowKeys::ChangePage),
          "arrow-key setter reports the change");
    check(!page.b.setCandidateArrowKeys(ari_ime::CandidateArrowKeys::ChangePage),
          "arrow-key setter is idempotent");
    page.type("su3cl3");       // 你好
    page.key(FcitxKey_Home);
    page.key(FcitxKey_Down);   // candidates for 你
    const auto firstPage = page.cand();
    check(page.b.isPicking(), "ChangePage setup opens the candidate window");
    check(page.b.candidatePage() == 1, "ChangePage setup starts at page 1");
    const int focused = page.b.selectionChar();
    check(focused >= 0, "ChangePage setup focuses a character");
    // How many pages libchewing offers for 你 is version-dependent (see
    // ISSUES.md), so drive the assertions off the reported count rather than
    // assuming a number. The invariants below hold for one page as well.
    const int lastPage = page.b.candidatePageCount();
    check(lastPage >= 1, "ChangePage setup reports at least one page");

    // The arrows page and cycle at both ends, like libchewing's own window.
    page.key(FcitxKey_Right);
    check(page.b.candidatePage() == (lastPage > 1 ? 2 : 1),
          "Right turns to the next page");
    check(page.b.selectionChar() == focused,
          "Right keeps the focus on the same character");
    if (lastPage > 1) {
        check(page.cand() != firstPage, "Right shows different candidates");
        page.key(FcitxKey_Left);
        check(page.b.candidatePage() == 1, "Left turns back a page");
        check(page.cand() == firstPage, "Left restores the first page");
    }

    page.key(FcitxKey_Home); // rebuild the list; leaves the page counter at 1
    check(page.b.candidatePage() == 1, "ChangePage wrap test starts at page 1");
    // Re-read the count: Home refeeds the run, so the rebuilt list is the one
    // the wrap assertions below actually page through.
    const int pages = page.b.candidatePageCount();
    page.key(FcitxKey_Left);
    check(page.b.candidatePage() == pages,
          "Left on the first page wraps to the last");
    page.key(FcitxKey_Right);
    check(page.b.candidatePage() == 1,
          "Right on the last page wraps to the first");
    for (int i = 0; i < pages; ++i) {
        page.key(FcitxKey_Right);
    }
    check(page.b.candidatePage() == 1,
          "a full lap of Right returns to the first page");
    check(page.b.selectionChar() == focused,
          "paging never leaves the focused character");

    // PageUp/PageDown keep their existing stop-at-the-end behavior.
    page.key(FcitxKey_Page_Up);
    check(page.b.candidatePage() == 1, "PageUp stops at the first page");
    page.key(FcitxKey_Page_Down);
    check(page.b.candidatePage() == (pages > 1 ? 2 : 1),
          "PageDown still advances");
    page.key(FcitxKey_Escape);
    check(page.b.isEditing() && !page.b.isPicking(),
          "Escape leaves the candidate window for caret mode");
    page.key(FcitxKey_Right);
    check(page.b.caretChar() == 1, "caret-mode Right still moves the caret");

    // The default keeps ←/→ walking between characters.
    Sim move;
    move.type("su3cl3");
    move.key(FcitxKey_Home);
    move.key(FcitxKey_Down);
    const int firstChar = move.b.selectionChar();
    move.key(FcitxKey_Right);
    check(move.b.selectionChar() == firstChar + 1,
          "MoveCursor keeps Right walking to the next character");

    // EndOfText restores the pre-2.6.3 behavior: a pick drops back to append.
    Sim endOfText;
    check(endOfText.b.setCaretAfterPick(ari_ime::CaretAfterPick::EndOfText),
          "caret setter reports the change");
    check(!endOfText.b.setCaretAfterPick(ari_ime::CaretAfterPick::EndOfText),
          "caret setter is idempotent");
    endOfText.type("su3cl3");
    endOfText.key(FcitxKey_Home);
    endOfText.key(FcitxKey_Down);
    const int niIndex = find_visible_candidate(endOfText.cand(), "妳");
    check(niIndex >= 0, "visible candidates include 妳 for the EndOfText test");
    check(endOfText.b.selectCandidate(niIndex).handled,
          "EndOfText test picks 妳");
    check_eq(endOfText.preedit(), "妳好", "EndOfText pick still rewrites the cell");
    check(!endOfText.b.isEditing() && endOfText.b.caretChar() == -1,
          "EndOfText leaves correction mode with the caret at the end");
    endOfText.type("1j4");
    check_eq(endOfText.preedit(), "妳好" + bu,
             "EndOfText resumes appending at the tail");
}

// ↑ on a literal key as Ari's original ASUS-style gesture: one key becomes the
// Bopomofo symbol it stands for, as a finished character.
void test_up_shows_bopomofo_symbol() {
    // Default: ↑ keeps folding literal keys into a Chinese character.
    Sim merge;
    merge.type("1");
    merge.key(FcitxKey_Up);
    check_eq(merge.preedit(), "1",
             "by default a lone key that forms no syllable is left alone");

    Sim s;
    check(s.b.setLiteralKeyReinterpret(
              ari_ime::LiteralKeyReinterpret::BopomofoSymbol),
          "literal-key setter reports the change");
    check(!s.b.setLiteralKeyReinterpret(
              ari_ime::LiteralKeyReinterpret::BopomofoSymbol),
          "literal-key setter is idempotent");

    s.type("1");
    check_eq(s.preedit(), "1", "a lone 注音 key stays literal while typing");
    s.key(FcitxKey_Up);
    check_eq(s.preedit(), "ㄅ", "Up turns the literal key into its Bopomofo symbol");
    check(s.b.isEditing() && !s.b.isPicking(),
          "the symbol does not open a candidate window");
    check(s.b.caretChar() == 1, "the caret parks just after the symbol");

    // The symbol is a finished character: the next key follows it rather than
    // continuing the syllable.
    s.type("j");
    check_eq(s.preedit(), "ㄅj", "typing after the symbol appends, not composes");
    s.key(FcitxKey_Return);
    check_eq(s.committed, "ㄅj", "the symbol commits as ordinary text");

    // Pressing Up again on the symbol is a no-op: it is no longer a raw key.
    Sim twice;
    twice.b.setLiteralKeyReinterpret(
        ari_ime::LiteralKeyReinterpret::BopomofoSymbol);
    twice.type("1");
    twice.key(FcitxKey_Up);
    twice.key(FcitxKey_Up);
    check_eq(twice.preedit(), "ㄅ", "a second Up leaves the symbol unchanged");

    // ↓ on the symbol must not mistake it for punctuation and open a list.
    Sim down;
    down.b.setLiteralKeyReinterpret(
        ari_ime::LiteralKeyReinterpret::BopomofoSymbol);
    down.type("1");
    down.key(FcitxKey_Up);
    down.key(FcitxKey_Down);
    check(!down.b.isPicking(), "Down on a Bopomofo symbol opens no candidates");
    check_eq(down.preedit(), "ㄅ", "Down leaves the symbol unchanged");

    // Tone keys carry their mark; only the focused key converts.
    Sim tone;
    tone.b.setLiteralKeyReinterpret(
        ari_ime::LiteralKeyReinterpret::BopomofoSymbol);
    tone.type("3");
    tone.key(FcitxKey_Up);
    check_eq(tone.preedit(), "ˇ", "Up on a tone key shows its tone mark");

    Sim one;
    one.b.setLiteralKeyReinterpret(
        ari_ime::LiteralKeyReinterpret::BopomofoSymbol);
    one.type("1j4");
    check_eq(one.preedit(), bu4_default(),
             "keys that complete a syllable still convert while typing");

    Sim mid;
    mid.b.setLiteralKeyReinterpret(
        ari_ime::LiteralKeyReinterpret::BopomofoSymbol);
    mid.type("cat");
    mid.key(FcitxKey_Home);
    mid.key(FcitxKey_Up);
    check_eq(mid.preedit(), "ㄏat",
             "only the character under the caret converts, not the run");
    check(mid.b.caretChar() == 1, "the caret follows the converted character");

    // A key with no 注音 slot on this layout is left untouched.
    Sim inert;
    inert.b.setLiteralKeyReinterpret(
        ari_ime::LiteralKeyReinterpret::BopomofoSymbol);
    inert.b.pasteAtCaret("@");
    inert.key(FcitxKey_Up);
    check_eq(inert.preedit(), "@", "Up leaves a non-注音 key alone");
}

void test_candidate_paging() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3");        // 你 has enough homophones to fill multiple pages
    s.key(FcitxKey_Down); // open candidates
    auto first = s.cand();
    check(static_cast<int>(first.size()) == 9, "first candidate page is full");
    int totalPages = s.b.candidatePageCount();
    check(s.b.candidatePage() == 1, "candidate paging starts at page 1");
    check(totalPages > 1, "candidate paging reports multiple pages");

    s.key(FcitxKey_Page_Down);
    auto second = s.cand();
    check(!second.empty(), "PageDown opens another candidate page");
    check(second != first, "PageDown changes candidate page contents");
    check(s.b.candidatePage() == 2, "PageDown advances page counter");
    check(s.b.candidatePageCount() == totalPages, "total page count is stable");

    s.key(FcitxKey_Page_Up);
    check(s.cand() == first, "PageUp returns to previous candidate page");
    check(s.b.candidatePage() == 1, "PageUp rewinds page counter");

    Sim pick;
    pick.type("su3cl3");       // 你好
    pick.key(FcitxKey_Left);   // caret between 你 and 好
    pick.key(FcitxKey_Left);   // caret before 你
    pick.key(FcitxKey_Down);   // open candidates for 你
    const auto page1 = pick.cand();
    check(static_cast<int>(page1.size()) > 2, "page1 has a slot 3");
    pick.key(FcitxKey_Page_Down);
    check(pick.b.candidatePage() == 2,
          "candidate pick setup reaches second page");
    const auto page2 = pick.cand();
    check(static_cast<int>(page2.size()) > 2, "page2 has a slot 3");
    check(page2[2] != page1[2], "page2 slot3 differs from page1 slot3");
    const std::string want = page2[2];
    pick.key('3');
    check_eq(utf8_char_at(pick.preedit(), 0), want,
             "page2 digit 3 applies visible page2 slot 3");
    check(pick.b.isEditing() && !pick.b.isPicking() &&
              pick.b.selectionChar() == -1,
          "cross-page pick closes the window but stays in caret mode");
    check(pick.b.caretChar() == 1,
          "cross-page pick parks the caret after the fixed character");
    pick.type("1j4");
    check_eq(pick.preedit(), want + bu + "好",
             "typing after cross-page pick continues at the corrected position");
}

void test_candidate_tab_navigation() {
    Sim empty;
    KeyResult r = empty.press(FcitxKey_Tab);
    check(!r.handled, "Tab without candidate window passes through");

    Sim s;
    s.type("su3");
    s.key(FcitxKey_Down);
    check(s.b.highlight() == 0, "candidate tab setup starts at first item");
    s.key(FcitxKey_Tab);
    check(s.b.highlight() == 1, "Tab advances candidate highlight");
    r = s.press(fcitx::Key(FcitxKey_Tab,
                           fcitx::KeyStates{fcitx::KeyState::Shift}));
    check(r.handled, "Shift+Tab is handled in candidate window");
    check(s.b.highlight() == 0, "Shift+Tab moves candidate highlight backward");
    s.key(FcitxKey_ISO_Left_Tab);
    check(s.b.candidatePage() == s.b.candidatePageCount(),
          "ISO_Left_Tab wraps to last candidate page");
}

void test_reinterpret() {
    Sim s;
    s.type("catsu3");     // cats + current ㄧˇ default (libchewing-dependent)
    std::string peeled = s.preedit();
    check(peeled.rfind("cats", 0) == 0 && utf8_count(peeled) == 5,
          "catsu3 keeps cats and peels one Chinese syllable");
    s.key(FcitxKey_Left); // caret between s and the peeled syllable
    s.key(FcitxKey_Left); // caret before s (so ↑ targets s)
    s.key(FcitxKey_Up);   // reinterpret the s (+ peeled syllable) -> 你
    check_eq(s.preedit(), "cat你", "reinterpret recovers cat你");

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
    if (ari_ime::isValidSyllable(".3-3", /*allowTone=*/true)) {
        Sim symbolLead;
        symbolLead.type(".3-3");
        symbolLead.key(FcitxKey_Home); // caret before '.'
        symbolLead.key(FcitxKey_Up);
        check(symbolLead.preedit() != ".3-3",
              "reinterpret recovers symbol-led zhuyin from boundary literal");
        check(symbolLead.b.selectionChar() == 0,
              "symbol-led reinterpret opens candidates on recovered character");
        check(!symbolLead.cand().empty(),
              "symbol-led reinterpret exposes candidate list");

        Sim afterChinese;
        afterChinese.type("su3");
        afterChinese.type(".3-3");
        afterChinese.key(FcitxKey_Home);
        afterChinese.key(FcitxKey_Right); // caret before '.'
        afterChinese.key(FcitxKey_Up);
        check(afterChinese.preedit() != "你.3-3",
              "symbol-led reinterpret also works after Chinese text");
        check(afterChinese.b.selectionChar() == 1,
              "symbol-led reinterpret after Chinese focuses recovered character");
    }

    Sim filename;
    filename.type("README.md");
    filename.key(FcitxKey_Home);
    filename.key(FcitxKey_Up);
    check_eq(filename.preedit(), "README.md",
             "reinterpret does not rewrite filename literal");
    check(filename.cand().empty(),
          "filename reinterpret no-op does not open candidates");

    Sim version;
    version.type("Ari-IME-1.0.0");
    version.key(FcitxKey_Home);
    version.key(FcitxKey_End);
    version.key(FcitxKey_Left); // caret before final 3
    version.key(FcitxKey_Left); // caret before '.'
    version.key(FcitxKey_Up);
    check_eq(version.preedit(), "Ari-IME-1.0.0",
             "reinterpret does not rewrite version punctuation");

    Sim word;
    word.type("release");
    word.key(FcitxKey_Home);
    word.key(FcitxKey_Up);
    check_eq(word.preedit(), "release",
             "reinterpret does not rewrite ordinary English word");

    Sim path;
    path.b.pasteAtCaret("src/su3.log");
    move_caret_to(path, 4); // before the s in /su3
    path.key(FcitxKey_Up);
    check_eq(path.preedit(), "src/su3.log",
             "reinterpret does not rewrite path segment");

    Sim command;
    command.b.pasteAtCaret("git checkout su3");
    move_caret_to(command, 13); // before the s in su3
    command.key(FcitxKey_Up);
    check_eq(command.preedit(), "git checkout su3",
             "reinterpret does not rewrite command argument");

    Sim code;
    code.b.pasteAtCaret("auto su3 = 1");
    move_caret_to(code, 5); // before the s in su3
    code.key(FcitxKey_Up);
    check_eq(code.preedit(), "auto su3 = 1",
             "reinterpret does not rewrite code identifier");

    Sim pipe;
    pipe.b.pasteAtCaret("cat input.txt | grep su3");
    move_caret_to(pipe, 21); // before the s in su3
    pipe.key(FcitxKey_Up);
    check_eq(pipe.preedit(), "cat input.txt | grep su3",
             "reinterpret does not rewrite shell pipeline argument");

    Sim query;
    query.b.pasteAtCaret("https://ari.test/search?q=su3&lang=zh");
    move_caret_to(query, 26); // before the s in q=su3
    query.key(FcitxKey_Up);
    check_eq(query.preedit(), "https://ari.test/search?q=su3&lang=zh",
             "reinterpret does not rewrite URL query value");

    Sim generic;
    generic.b.pasteAtCaret("std::vector<su3> values");
    move_caret_to(generic, 12); // before the s in <su3>
    generic.key(FcitxKey_Up);
    check_eq(generic.preedit(), "std::vector<su3> values",
             "reinterpret does not rewrite generic type argument");

    Sim markdown;
    markdown.b.pasteAtCaret("`su3` should stay literal");
    move_caret_to(markdown, 1); // before the s inside inline code
    markdown.key(FcitxKey_Up);
    check_eq(markdown.preedit(), "`su3` should stay literal",
             "reinterpret does not rewrite Markdown inline code");

    Sim json;
    json.b.pasteAtCaret("{\"key\":\"su3\"}");
    move_caret_to(json, 8); // before the s in the JSON value
    json.key(FcitxKey_Up);
    check_eq(json.preedit(), "{\"key\":\"su3\"}",
             "reinterpret does not rewrite JSON string value");

    Sim snake;
    snake.b.pasteAtCaret("config_su3_value");
    move_caret_to(snake, 7); // before the s after underscore
    snake.key(FcitxKey_Up);
    check_eq(snake.preedit(), "config_su3_value",
             "reinterpret does not rewrite snake_case identifier");

    Sim log;
    log.b.pasteAtCaret("2026-06-19T12:34:56Z level=info code=su3");
    move_caret_to(log, 39); // before the s in code=su3
    log.key(FcitxKey_Up);
    check_eq(log.preedit(), "2026-06-19T12:34:56Z level=info code=su3",
             "reinterpret does not rewrite log key-value field");

    Sim sql;
    sql.b.pasteAtCaret("SELECT su3 FROM users WHERE id=1");
    move_caret_to(sql, 7); // before the s in selected column
    sql.key(FcitxKey_Up);
    check_eq(sql.preedit(), "SELECT su3 FROM users WHERE id=1",
             "reinterpret does not rewrite SQL identifier");

    Sim css;
    css.b.pasteAtCaret(".btn-su3:hover { color: red; }");
    move_caret_to(css, 5); // before the s in .btn-su3
    css.key(FcitxKey_Up);
    check_eq(css.preedit(), ".btn-su3:hover { color: red; }",
             "reinterpret does not rewrite CSS selector segment");

    Sim yaml;
    yaml.b.pasteAtCaret("su3: enabled");
    move_caret_to(yaml, 0); // before the YAML key
    yaml.key(FcitxKey_Up);
    check_eq(yaml.preedit(), "su3: enabled",
             "reinterpret does not rewrite YAML key");

    Sim toml;
    toml.b.pasteAtCaret("su3 = true");
    move_caret_to(toml, 0); // before the TOML key
    toml.key(FcitxKey_Up);
    check_eq(toml.preedit(), "su3 = true",
             "reinterpret does not rewrite TOML key");

    Sim docker;
    docker.b.pasteAtCaret("su3:latest");
    move_caret_to(docker, 0); // before the Docker image name
    docker.key(FcitxKey_Up);
    check_eq(docker.preedit(), "su3:latest",
             "reinterpret does not rewrite Docker image tag");

    Sim regex;
    regex.b.pasteAtCaret("su3+");
    move_caret_to(regex, 0); // before the regex atom
    regex.key(FcitxKey_Up);
    check_eq(regex.preedit(), "su3+",
             "reinterpret does not rewrite regex token");

    Sim gitref;
    gitref.b.pasteAtCaret("su3/main");
    move_caret_to(gitref, 0); // before the Git ref prefix
    gitref.key(FcitxKey_Up);
    check_eq(gitref.preedit(), "su3/main",
             "reinterpret does not rewrite Git ref prefix");

    Sim hostport;
    hostport.b.pasteAtCaret("su3.example:443");
    move_caret_to(hostport, 0); // before the hostname
    hostport.key(FcitxKey_Up);
    check_eq(hostport.preedit(), "su3.example:443",
             "reinterpret does not rewrite hostname or host:port");

    Sim makefile;
    makefile.b.pasteAtCaret("su3: build");
    move_caret_to(makefile, 0); // before the Makefile target
    makefile.key(FcitxKey_Up);
    check_eq(makefile.preedit(), "su3: build",
             "reinterpret does not rewrite Makefile target");

    Sim ipv6;
    ipv6.b.pasteAtCaret("su3::1");
    move_caret_to(ipv6, 0); // before the IPv6-like literal
    ipv6.key(FcitxKey_Up);
    check_eq(ipv6.preedit(), "su3::1",
             "reinterpret does not rewrite IPv6-like literal");

    Sim templateVar;
    templateVar.b.pasteAtCaret("su3}}");
    move_caret_to(templateVar, 0); // before the template variable name
    templateVar.key(FcitxKey_Up);
    check_eq(templateVar.preedit(), "su3}}",
             "reinterpret does not rewrite template variable");

    Sim shellVar;
    shellVar.b.pasteAtCaret("$su3");
    move_caret_to(shellVar, 1); // before the shell variable name
    shellVar.key(FcitxKey_Up);
    check_eq(shellVar.preedit(), "$su3",
             "reinterpret does not rewrite shell variable");

    Sim envAssign;
    envAssign.b.pasteAtCaret("su3=value");
    move_caret_to(envAssign, 0); // before the environment variable name
    envAssign.key(FcitxKey_Up);
    check_eq(envAssign.preedit(), "su3=value",
             "reinterpret does not rewrite environment assignment");

    Sim templateFilter;
    templateFilter.b.pasteAtCaret("su3|upper");
    move_caret_to(templateFilter, 0); // before the template value
    templateFilter.key(FcitxKey_Up);
    check_eq(templateFilter.preedit(), "su3|upper",
             "reinterpret does not rewrite template filter");

    Sim route;
    route.b.pasteAtCaret("/items/:su3");
    move_caret_to(route, 8); // before the route parameter name
    route.key(FcitxKey_Up);
    check_eq(route.preedit(), "/items/:su3",
             "reinterpret does not rewrite framework route parameter");

    Sim glob;
    glob.b.pasteAtCaret("su3*");
    move_caret_to(glob, 0); // before the glob stem
    glob.key(FcitxKey_Up);
    check_eq(glob.preedit(), "su3*",
             "reinterpret does not rewrite glob pattern");

    Sim makeVar;
    makeVar.b.pasteAtCaret("$(su3)");
    move_caret_to(makeVar, 2); // before the Make variable name
    makeVar.key(FcitxKey_Up);
    check_eq(makeVar.preedit(), "$(su3)",
             "reinterpret does not rewrite Make variable expansion");

    Sim cmakeVar;
    cmakeVar.b.pasteAtCaret("${su3}");
    move_caret_to(cmakeVar, 2); // before the CMake variable name
    cmakeVar.key(FcitxKey_Up);
    check_eq(cmakeVar.preedit(), "${su3}",
             "reinterpret does not rewrite CMake variable expansion");

    Sim vue;
    vue.b.pasteAtCaret("{{ su3 | upper }}");
    move_caret_to(vue, 3); // before the Vue template expression value
    vue.key(FcitxKey_Up);
    check_eq(vue.preedit(), "{{ su3 | upper }}",
             "reinterpret does not rewrite Vue template expression");

    Sim react;
    react.b.pasteAtCaret("{su3 && item}");
    move_caret_to(react, 1); // before the React expression identifier
    react.key(FcitxKey_Up);
    check_eq(react.preedit(), "{su3 && item}",
             "reinterpret does not rewrite React expression identifier");

    Sim csv;
    csv.b.pasteAtCaret("su3,amount");
    move_caret_to(csv, 0); // before the CSV field
    csv.key(FcitxKey_Up);
    check_eq(csv.preedit(), "su3,amount",
             "reinterpret does not rewrite CSV field");

    Sim tsv;
    tsv.b.pasteAtCaret("su3\tamount");
    move_caret_to(tsv, 0); // before the TSV field
    tsv.key(FcitxKey_Up);
    check_eq(tsv.preedit(), "su3 amount",
             "reinterpret does not rewrite TSV field");

    Sim formula;
    formula.b.pasteAtCaret("=su3+1");
    move_caret_to(formula, 1); // before the formula identifier
    formula.key(FcitxKey_Up);
    check_eq(formula.preedit(), "=su3+1",
             "reinterpret does not rewrite spreadsheet-like formula");

    Sim latex;
    latex.b.pasteAtCaret("\\su3{}");
    move_caret_to(latex, 1); // before the LaTeX command name
    latex.key(FcitxKey_Up);
    check_eq(latex.preedit(), "\\su3{}",
             "reinterpret does not rewrite LaTeX command");

    Sim markdownAttr;
    markdownAttr.b.pasteAtCaret("[label]{#su3}");
    move_caret_to(markdownAttr, 9); // before the Markdown attribute id
    markdownAttr.key(FcitxKey_Up);
    check_eq(markdownAttr.preedit(), "[label]{#su3}",
             "reinterpret does not rewrite Markdown attribute id");

    Sim logBracket;
    logBracket.b.pasteAtCaret("[su3] request started");
    move_caret_to(logBracket, 1); // before the log tag
    logBracket.key(FcitxKey_Up);
    check_eq(logBracket.preedit(), "[su3] request started",
             "reinterpret does not rewrite bracketed log tag");

    Sim logJsonish;
    logJsonish.b.pasteAtCaret("event=su3, status=ok");
    move_caret_to(logJsonish, 6); // before the log field value
    logJsonish.key(FcitxKey_Up);
    check_eq(logJsonish.preedit(), "event=su3, status=ok",
             "reinterpret does not rewrite comma-delimited log value");

    Sim notebookCell;
    notebookCell.b.pasteAtCaret("# %% su3");
    move_caret_to(notebookCell, 5); // before the notebook cell tag
    notebookCell.key(FcitxKey_Up);
    check_eq(notebookCell.preedit(), "# %% su3",
             "reinterpret does not rewrite notebook cell marker text");

    Sim pandasColumn;
    pandasColumn.b.pasteAtCaret("df['su3']");
    move_caret_to(pandasColumn, 4); // before the dataframe column name
    pandasColumn.key(FcitxKey_Up);
    check_eq(pandasColumn.preedit(), "df['su3']",
             "reinterpret does not rewrite quoted dataframe column");

    Sim templatedSql;
    templatedSql.b.pasteAtCaret("{{ ref('su3') }}");
    move_caret_to(templatedSql, 8); // before the dbt/Jinja relation name
    templatedSql.key(FcitxKey_Up);
    check_eq(templatedSql.preedit(), "{{ ref('su3') }}",
             "reinterpret does not rewrite templated SQL relation name");

    Sim kubeEnv;
    kubeEnv.b.pasteAtCaret("value: $(su3)");
    move_caret_to(kubeEnv, 9); // before the Kubernetes env var name
    kubeEnv.key(FcitxKey_Up);
    check_eq(kubeEnv.preedit(), "value: $(su3)",
             "reinterpret does not rewrite Kubernetes env ref");

    Sim graphqlFragment;
    graphqlFragment.b.pasteAtCaret("...su3 on User");
    move_caret_to(graphqlFragment, 3); // before the GraphQL fragment name
    graphqlFragment.key(FcitxKey_Up);
    check_eq(graphqlFragment.preedit(), "...su3 on User",
             "reinterpret does not rewrite GraphQL fragment name");

    Sim graphqlVariable;
    graphqlVariable.b.pasteAtCaret("$su3: String!");
    move_caret_to(graphqlVariable, 1); // before the GraphQL variable name
    graphqlVariable.key(FcitxKey_Up);
    check_eq(graphqlVariable.preedit(), "$su3: String!",
             "reinterpret does not rewrite GraphQL variable");

    Sim terraformResource;
    terraformResource.b.pasteAtCaret("resource \"x\" \"su3\" {}");
    move_caret_to(terraformResource, 14); // before the HCL resource name
    terraformResource.key(FcitxKey_Up);
    check_eq(terraformResource.preedit(), "resource \"x\" \"su3\" {}",
             "reinterpret does not rewrite Terraform resource name");

    Sim hclIdentifier;
    hclIdentifier.b.pasteAtCaret("local.su3");
    move_caret_to(hclIdentifier, 6); // before the HCL identifier
    hclIdentifier.key(FcitxKey_Up);
    check_eq(hclIdentifier.preedit(), "local.su3",
             "reinterpret does not rewrite HCL dotted identifier");

    Sim protobufField;
    protobufField.b.pasteAtCaret("optional string su3 = 1;");
    move_caret_to(protobufField, 16); // before the protobuf field name
    protobufField.key(FcitxKey_Up);
    check_eq(protobufField.preedit(), "optional string su3 = 1;",
             "reinterpret does not rewrite protobuf field name");

    Sim schemaType;
    schemaType.b.pasteAtCaret("type Su3 { id: ID! }");
    move_caret_to(schemaType, 5); // before the schema type name
    schemaType.key(FcitxKey_Up);
    check_eq(schemaType.preedit(), "type Su3 { id: ID! }",
             "reinterpret does not rewrite schema type name");

    Sim promMetric;
    promMetric.b.pasteAtCaret("rate(su3_total{job=\"api\"}[5m])");
    move_caret_to(promMetric, 5); // before the PromQL metric name
    promMetric.key(FcitxKey_Up);
    check_eq(promMetric.preedit(), "rate(su3_total{job=\"api\"}[5m])",
             "reinterpret does not rewrite PromQL metric name");

    Sim promLabel;
    promLabel.b.pasteAtCaret("up{su3=\"api\"}");
    move_caret_to(promLabel, 3); // before the PromQL label name
    promLabel.key(FcitxKey_Up);
    check_eq(promLabel.preedit(), "up{su3=\"api\"}",
             "reinterpret does not rewrite PromQL label name");

    Sim ciExpression;
    ciExpression.b.pasteAtCaret("${{ env.su3 }}");
    move_caret_to(ciExpression, 8); // before the GitHub Actions property name
    ciExpression.key(FcitxKey_Up);
    check_eq(ciExpression.preedit(), "${{ env.su3 }}",
             "reinterpret does not rewrite CI expression property");

    Sim htmlAttr;
    htmlAttr.b.pasteAtCaret("<div id=\"su3\">");
    move_caret_to(htmlAttr, 9); // before the HTML attribute value
    htmlAttr.key(FcitxKey_Up);
    check_eq(htmlAttr.preedit(), "<div id=\"su3\">",
             "reinterpret does not rewrite HTML attribute value");

    Sim htmlDataAttr;
    htmlDataAttr.b.pasteAtCaret("data-su3=\"x\"");
    move_caret_to(htmlDataAttr, 5); // before the HTML data attribute stem
    htmlDataAttr.key(FcitxKey_Up);
    check_eq(htmlDataAttr.preedit(), "data-su3=\"x\"",
             "reinterpret does not rewrite HTML data attribute name");

    Sim rstAnchor;
    rstAnchor.b.pasteAtCaret(".. _su3:");
    move_caret_to(rstAnchor, 4); // before the reStructuredText anchor name
    rstAnchor.key(FcitxKey_Up);
    check_eq(rstAnchor.preedit(), ".. _su3:",
             "reinterpret does not rewrite reStructuredText anchor");

    Sim rstRole;
    rstRole.b.pasteAtCaret(":ref:`su3`");
    move_caret_to(rstRole, 6); // before the reStructuredText role target
    rstRole.key(FcitxKey_Up);
    check_eq(rstRole.preedit(), ":ref:`su3`",
             "reinterpret does not rewrite reStructuredText role target");

    Sim systemdUnit;
    systemdUnit.b.pasteAtCaret("su3.service");
    move_caret_to(systemdUnit, 0); // before the systemd unit name
    systemdUnit.key(FcitxKey_Up);
    check_eq(systemdUnit.preedit(), "su3.service",
             "reinterpret does not rewrite systemd unit name");

    Sim ciStepOutput;
    ciStepOutput.b.pasteAtCaret("steps.su3.outputs.path");
    move_caret_to(ciStepOutput, 6); // before the GitHub Actions step id
    ciStepOutput.key(FcitxKey_Up);
    check_eq(ciStepOutput.preedit(), "steps.su3.outputs.path",
             "reinterpret does not rewrite CI step output property");

    Sim dockerCompose;
    dockerCompose.b.pasteAtCaret("depends_on: [su3]");
    move_caret_to(dockerCompose, 13); // before the compose service name
    dockerCompose.key(FcitxKey_Up);
    check_eq(dockerCompose.preedit(), "depends_on: [su3]",
             "reinterpret does not rewrite Docker Compose service reference");

    Sim npmScope;
    npmScope.b.pasteAtCaret("@su3/package");
    move_caret_to(npmScope, 1); // before the npm scope name
    npmScope.key(FcitxKey_Up);
    check_eq(npmScope.preedit(), "@su3/package",
             "reinterpret does not rewrite npm package scope");
}

void test_insert_while_selecting() {
    Sim s;
    s.type("fie");
    s.key(FcitxKey_Left); // caret between i and e
    s.key('l');           // insert before e
    check_eq(s.preedit(), "file", "insert l before e -> file");
}

void test_commit_after_pick() {
    // Picking then committing must output exactly the current pre-edit (the
    // learning replay must not alter it). A pick drops to caret mode; Enter commits.
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left);     // caret between 你 and 好
    s.key(FcitxKey_Left);     // caret before 你
    s.key(FcitxKey_Down);     // open candidates for 你
    int phraseIndex = find_visible_candidate(s.cand(), "妳好");
    check(phraseIndex >= 0, "visible candidates include 妳好");
    KeyResult picked = s.b.selectCandidate(phraseIndex);
    check(picked.handled, "direct phrase pick selects 妳好");
    std::string chosen = s.preedit();
    s.key(FcitxKey_Return);   // commit
    check_eq(s.committed, chosen, "commit reflects current pre-edit");
    check_eq(s.preedit(), "", "preedit cleared");
}

void test_selection_backspace() {
    Sim s;
    s.type("su3cl3");          // 你好
    s.key(FcitxKey_Left);      // caret between 你 and 好
    s.key(FcitxKey_BackSpace); // delete the char left of the caret (你)
    check_eq(s.preedit(), "好", "caret backspace deletes the char before it");
    check(s.b.selectionChar() == -1, "no candidate window after backspace");
}

void test_caret_delete_home_end() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3");       // 你好
    s.key(FcitxKey_Left);   // caret between 你 and 好
    s.key(FcitxKey_Delete); // delete the char right of the caret (好)
    check_eq(s.preedit(), "你", "caret delete removes char to the right");

    Sim h;
    h.type("su3cl3");       // 你好
    h.key(FcitxKey_Left);   // enter caret mode
    h.key(FcitxKey_Home);   // caret before 你
    h.type("1j4");          // ㄅㄨˋ
    check_eq(h.preedit(), bu + "你好", "Home moves insertion to the front");

    Sim e;
    e.type("su3cl3");       // 你好
    e.key(FcitxKey_Left);   // caret between 你 and 好
    e.key(FcitxKey_Home);   // front
    e.key(FcitxKey_End);    // end
    e.type("1j4");          // ㄅㄨˋ
    check_eq(e.preedit(), "你好" + bu, "End moves insertion to the end");
}

void test_phrase_cursor_navigation() {
    Sim chinese;
    chinese.type("su3cl3");
    chinese.press(fcitx::Key(FcitxKey_Left, fcitx::KeyState::Ctrl));
    check(chinese.b.isEditing(), "Ctrl+Left enters caret editing");
    check(chinese.b.caretChar() == 0,
          "Ctrl+Left follows libchewing phrase boundary");
    chinese.press(fcitx::Key(FcitxKey_Right, fcitx::KeyState::Ctrl));
    check(chinese.b.caretChar() == 2,
          "Ctrl+Right advances over the recognized Chinese phrase");

    Sim english;
    english.b.pasteAtCaret("alpha beta");
    english.press(fcitx::Key(FcitxKey_Left, fcitx::KeyState::Ctrl));
    check(english.b.caretChar() == 6,
          "Ctrl+Left moves over one English word");
    english.press(fcitx::Key(FcitxKey_Left, fcitx::KeyState::Ctrl));
    check(english.b.caretChar() == 5,
          "Ctrl+Left treats separating space as its own boundary");
    english.press(fcitx::Key(FcitxKey_Left, fcitx::KeyState::Ctrl));
    check(english.b.caretChar() == 0,
          "Ctrl+Left reaches the previous English word boundary");

    KeyResult shifted = english.press(fcitx::Key(
        FcitxKey_Right,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift}));
    check(!shifted.handled,
          "Ctrl+Shift+Arrow remains available for application text selection");
}

void test_long_chinese_candidate_window_alignment() {
    Sim s;
    for (int i = 0; i < 14; ++i) {
        s.type("su3cl31j4");
    }
    const std::string before = s.preedit();
    check(utf8_count(before) == 42,
          "long candidate test crosses the expanded chewing active window");
    const std::string first = utf8_char_at(before, 0);

    s.key(FcitxKey_Home);
    s.key(FcitxKey_Down);
    const auto candidates = s.cand();
    check(!candidates.empty(), "long preedit opens candidates at the first cell");
    check(!candidates.empty() && utf8_char_at(candidates.front(), 0) == first,
          "long preedit candidate window stays aligned with the first cell");
    check(s.b.selectionChar() == 0,
          "long preedit selection marker stays on the requested first cell");
}

void test_direct_navigation_enters_editing() {
    const std::string bu = bu4_default();

    Sim empty;
    KeyResult r = empty.press(FcitxKey_Home);
    check(!r.handled, "Home with no pre-edit passes through");
    r = empty.press(FcitxKey_Up);
    check(!r.handled, "Up with no pre-edit passes through");

    Sim h;
    h.type("su3cl3");      // 你好
    h.key(FcitxKey_Home);  // direct caret mode at front
    h.type("1j4");         // ㄅㄨˋ
    check_eq(h.preedit(), bu + "你好", "top-level Home enters editing at front");

    Sim b;
    b.type("su3cl3");       // 你好
    b.key(FcitxKey_Begin);  // direct Begin at front
    b.type("1j4");          // ㄅㄨˋ
    check_eq(b.preedit(), bu + "你好", "top-level Begin enters editing at front");

    Sim e;
    e.type("su3cl3");      // 你好
    e.key(FcitxKey_Home);  // front
    e.key(FcitxKey_End);   // direct End to tail while editing
    e.type("1j4");         // ㄅㄨˋ
    check_eq(e.preedit(), "你好" + bu, "top-level End moves editing caret to tail");

    Sim d;
    d.type("su3");
    r = d.press(FcitxKey_End);
    check(r.handled, "top-level End enters editing with pre-edit");

    Sim u;
    u.type("su3");
    u.key(FcitxKey_Up);
    check(u.b.selectionChar() == 0, "top-level Up opens candidates on pre-edit");
    check(!u.cand().empty(), "top-level Up shows candidates");
}

void test_escape_behavior() {
    const std::string bu = bu4_default();

    Sim empty;
    KeyResult r = empty.press(FcitxKey_Escape);
    check(!r.handled, "Escape with no pre-edit passes through");

    Sim active;
    active.type("su3");
    r = active.press(FcitxKey_Escape);
    check(r.handled, "Escape clears active pre-edit");
    check_eq(active.preedit(), "", "Escape leaves no pre-edit");

    Sim picking;
    picking.type("su3");
    picking.key(FcitxKey_Down);
    check(picking.b.selectionChar() == 0, "Escape setup has candidate window");
    picking.key(FcitxKey_Escape);
    check(picking.b.selectionChar() == -1, "Escape closes candidate window");
    check_eq(picking.preedit(), "你", "Escape keeps text after closing candidates");
    picking.type("1j4");
    check_eq(picking.preedit(), bu + "你",
             "typing after candidate Escape resumes at caret");

    Sim caret;
    caret.type("su3cl3");
    caret.key(FcitxKey_Home);
    caret.key(FcitxKey_Escape);
    check_eq(caret.preedit(), "你好", "Escape exits caret mode without clearing");
    caret.type("1j4");
    check_eq(caret.preedit(), "你好" + bu,
             "typing after caret Escape resumes at end");
}

void test_candidate_control_closes_to_caret() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3");         // 你好
    s.key(FcitxKey_Left);     // caret between 你 and 好
    s.key(FcitxKey_Left);     // caret before 你
    s.key(FcitxKey_Down);     // candidate window focused on 你
    KeyResult r = s.press(FcitxKey_F1);
    check(!r.handled, "non-printable control passes through after closing");
    check(s.b.selectionChar() == -1, "control key closes candidate highlight");
    check_eq(s.preedit(), "你好", "control key keeps pre-edit text");
    s.type("1j4");
    check_eq(s.preedit(), bu + "你好",
             "typing after control-close resumes at focused caret");
}

void test_candidate_modifier_keeps_window() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3");
    s.key(FcitxKey_Down);
    check(s.b.selectionChar() == 0, "modifier test starts with candidate window");
    const std::string before = s.preedit();

    KeyResult shift = s.press(FcitxKey_Shift_L);
    check(!shift.handled, "bare Shift is not consumed by candidate picking");
    check(!shift.updateUI, "bare Shift does not refresh the candidate UI");
    check(s.b.selectionChar() == 0, "bare Shift keeps the candidate window open");
    check_eq(s.preedit(), before, "bare Shift does not change pre-edit text");

    KeyResult shiftRight = s.press(FcitxKey_Shift_R);
    check(!shiftRight.handled, "bare Shift_R is not consumed by candidate picking");
    check(s.b.selectionChar() == 0, "bare Shift_R keeps the candidate window open");

    KeyResult ctrl = s.press(FcitxKey_Control_L);
    check(!ctrl.handled, "bare Ctrl is not consumed by candidate picking");
    check(s.b.selectionChar() == 0, "bare Ctrl keeps the candidate window open");

    KeyResult forgotten = s.press(fcitx::Key(
        FcitxKey_Delete, fcitx::KeyStates{fcitx::KeyState::Shift}));
    check(forgotten.handled,
          "Shift+Delete after bare Shift is still handled in picking");
    check_eq(s.preedit(), before,
             "Shift+Delete after bare Shift does not delete the focused cell");
    check(s.b.selectionChar() == 0,
          "Shift+Delete after bare Shift stays in candidate picking");

    Sim caret;
    caret.type("su3cl3");
    caret.key(FcitxKey_Home);
    check(caret.b.selectionChar() == -1, "caret setup has no candidate window");
    caret.press(FcitxKey_Shift_L);
    caret.type("1j4");
    check_eq(caret.preedit(), bu + "你好",
             "bare Shift in caret mode does not exit to end-of-preedit typing");
}

void test_candidate_right_reaches_end() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3"); // 你好
    s.key(FcitxKey_Down); // candidate window starts on the final 好
    s.key(FcitxKey_Right); // move past the final cell to the append position
    check(s.b.selectionChar() == -1,
          "Right past the final candidate closes the candidate window");
    check(s.b.caretChar() == 2,
          "Right past the final candidate places the caret at the end");
    s.type("1j4");
    check_eq(s.preedit(), "你好" + bu,
             "typing after moving past the final candidate appends at the end");
}

void test_picking_delete_focused_cell() {
    const std::string bu = bu4_default();

    Sim s;
    s.type("su3cl3");          // 你好
    s.key(FcitxKey_Left);      // caret between 你 and 好
    s.key(FcitxKey_Left);      // caret before 你
    s.key(FcitxKey_Down);      // candidate window focused on 你
    s.key(FcitxKey_BackSpace); // delete focused 你, not char left of caret
    check_eq(s.preedit(), "好", "picking backspace deletes focused cell");
    check(s.b.selectionChar() == -1, "candidate window closes after focused delete");
    s.type("1j4");
    check_eq(s.preedit(), bu + "好",
             "typing after focused delete resumes at deleted position");

    Sim d;
    d.type("su3cl3");          // 你好
    d.key(FcitxKey_Down);      // candidate window focused on 好 (last char)
    d.key(FcitxKey_Delete);
    check_eq(d.preedit(), "你", "picking delete removes focused cell");

    Sim h;
    h.type("su3cl3");          // 你好
    h.key(FcitxKey_Down);      // focused on 好
    h.key(FcitxKey_Home);      // focused on 你
    h.key(FcitxKey_Delete);
    check_eq(h.preedit(), "好", "picking Home jumps to first cell");

    Sim e;
    e.type("su3cl3");          // 你好
    e.key(FcitxKey_Left);      // caret between 你 and 好
    e.key(FcitxKey_Left);      // caret before 你
    e.key(FcitxKey_Down);      // focused on 你
    e.key(FcitxKey_End);       // focused on 好
    e.key(FcitxKey_Delete);
    check_eq(e.preedit(), "你", "picking End jumps to last cell");
}

void test_fullwidth_punct() {
    // Default punctuation is literal regardless of surrounding language. Users
    // request Chinese punctuation explicitly with Ctrl+Shift.
    Sim h;
    h.type("su3");
    h.key('<');
    h.key('?');
    check_eq(h.preedit(), "你<?", "default keeps punctuation after Chinese literal");

    Sim literalEnglish;
    literalEnglish.type("API");
    literalEnglish.key('?');
    check_eq(literalEnglish.preedit(), "API?",
             "default keeps punctuation after English half-width");

    Sim explicitChinese;
    explicitChinese.type("su3");
    KeyResult explicitComma = explicitChinese.press(fcitx::Key(
        FcitxKey_less,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift}));
    KeyResult explicitQuestion = explicitChinese.press(fcitx::Key(
        FcitxKey_question,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift}));
    check(explicitComma.handled && explicitQuestion.handled,
          "Ctrl+Shift punctuation is handled explicitly");
    check_eq(explicitChinese.preedit(), "你，？",
             "Ctrl+Shift punctuation produces Chinese forms");

    Sim explicitAfterEnglish;
    explicitAfterEnglish.type("API");
    KeyResult explicitEnglishQuestion = explicitAfterEnglish.press(fcitx::Key(
        FcitxKey_question,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift}));
    check(explicitEnglishQuestion.handled,
          "Ctrl+Shift punctuation is independent of English context");
    check_eq(explicitAfterEnglish.preedit(), "API？",
             "explicit Chinese punctuation also works after English");

    Sim explicitMidstring;
    explicitMidstring.type("su3cl3");
    explicitMidstring.key(FcitxKey_Left);
    KeyResult explicitMiddle = explicitMidstring.press(fcitx::Key(
        FcitxKey_less,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift}));
    check(explicitMiddle.handled,
          "Ctrl+Shift punctuation works while caret editing");
    check_eq(explicitMidstring.preedit(), "你，好",
             "explicit Chinese punctuation inserts at the caret");

    // Full-width mode: non-注音 punctuation keys become Chinese punctuation, while
    // 注音 韻母 keys (',' = ㄝ) still form bopomofo.
    Sim f;
    check(f.b.setFullWidthPunct(true), "fullwidth setter reports change");
    check(!f.b.setFullWidthPunct(true), "fullwidth setter is idempotent");
    f.type("su3");
    f.key('<');               // ，
    check_eq(f.preedit(), "你，", "fullwidth comma via <");
    f.key('>');               // 。
    check_eq(f.preedit(), "你，。", "fullwidth period via >");
    f.key('?');               // ？
    check_eq(f.preedit(), "你，。？", "fullwidth question mark");

    Sim quote;
    quote.b.setFullWidthPunct(true);
    quote.key('[');            // 「
    quote.type("su3");
    quote.key(']');            // 」
    quote.key('\'');           // 、
    check_eq(quote.preedit(), "「你」、", "fullwidth corner quotes and dunhao");

    Sim ctrlShiftDunhao;
    ctrlShiftDunhao.type("su3");
    KeyResult ctrlShiftDunhaoResult = ctrlShiftDunhao.press(fcitx::Key(
        FcitxKey_quotedbl,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift}));
    check(ctrlShiftDunhaoResult.handled,
          "Ctrl+Shift+' is handled as Chinese punctuation");
    check_eq(ctrlShiftDunhao.preedit(), "你、", "Ctrl+Shift+' inserts dunhao");

    Sim normalizedCtrlShiftDunhao;
    normalizedCtrlShiftDunhao.type("su3");
    KeyResult normalizedResult = normalizedCtrlShiftDunhao.press(
        fcitx::Key(FcitxKey_quotedbl, fcitx::KeyState::Ctrl));
    check(normalizedResult.handled,
          "normalized Ctrl+Shift+' is handled as Chinese punctuation");
    check_eq(normalizedCtrlShiftDunhao.preedit(), "你、",
             "normalized Ctrl+Shift+' inserts dunhao");

    Sim altShiftDunhao;
    check(altShiftDunhao.b.setChinesePunctuationShortcut(
              ari_ime::ChinesePunctuationShortcut::AltShift),
          "punctuation shortcut can switch to Alt+Shift");
    KeyResult altShiftResult = altShiftDunhao.press(fcitx::Key(
        FcitxKey_quotedbl,
        fcitx::KeyStates{fcitx::KeyState::Alt, fcitx::KeyState::Shift}));
    check(altShiftResult.handled,
          "configured Alt+Shift punctuation is handled");
    check_eq(altShiftDunhao.preedit(), "、",
             "configured Alt+Shift inserts dunhao");

    Sim ctrlDunhao;
    check(ctrlDunhao.b.setChinesePunctuationShortcut(
              ari_ime::ChinesePunctuationShortcut::Control),
          "punctuation shortcut can switch to Ctrl");
    KeyResult ctrlOnlyResult = ctrlDunhao.press(
        fcitx::Key(FcitxKey_apostrophe, fcitx::KeyState::Ctrl));
    check(ctrlOnlyResult.handled, "configured Ctrl punctuation is handled");
    check_eq(ctrlDunhao.preedit(), "、", "configured Ctrl inserts dunhao");

    Sim disabledPunctuation;
    check(disabledPunctuation.b.setChinesePunctuationShortcut(
              ari_ime::ChinesePunctuationShortcut::Disabled),
          "punctuation shortcut can be disabled");
    KeyResult disabledResult = disabledPunctuation.press(fcitx::Key(
        FcitxKey_quotedbl,
        fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Shift}));
    check(!disabledResult.handled,
          "disabled punctuation shortcut remains available to applications");
    check(disabledPunctuation.preedit().empty(),
          "disabled punctuation shortcut does not insert text");

    Sim spaceCandidates;
    check(spaceCandidates.b.setSpaceCandidateMode(true),
          "Space candidate compatibility mode can be enabled");
    spaceCandidates.type("hk4g4");
    KeyResult spaceResult = spaceCandidates.press(FcitxKey_space);
    check(spaceResult.handled && spaceCandidates.b.isPicking(),
          "Space opens the candidate window in compatibility mode");
    check(find_visible_candidate(spaceCandidates.cand(), "測試") >= 0,
          "Space candidate mode keeps the current phrase candidate visible");

    Sim defaultSpace;
    defaultSpace.type("hk4g4");
    defaultSpace.key(FcitxKey_space);
    check(!defaultSpace.b.isPicking(),
          "default Space behavior remains outside candidate mode");

    Sim ctrlPassthrough;
    KeyResult ctrlResult = ctrlPassthrough.press(
        fcitx::Key(FcitxKey_apostrophe, fcitx::KeyState::Ctrl));
    check(!ctrlResult.handled, "Ctrl+' remains available to applications");

    Sim altPassthrough;
    KeyResult altResult = altPassthrough.press(
        fcitx::Key(FcitxKey_apostrophe, fcitx::KeyState::Alt));
    check(!altResult.handled, "Alt+' remains available to applications");

    KeyResult altQuestionResult = altPassthrough.press(
        fcitx::Key(FcitxKey_question,
                   fcitx::KeyStates{fcitx::KeyState::Alt,
                                    fcitx::KeyState::Shift}));
    check(!altQuestionResult.handled,
          "Alt punctuation remains available to applications");

    Sim paired;
    paired.b.setFullWidthPunct(true);
    paired.key('(');            // （
    paired.type("su3");
    paired.key(')');            // ）
    paired.key('{');            // 『
    paired.type("cl3");
    paired.key('}');            // 』
    paired.key('!');            // ！
    paired.key(':');            // ：
    paired.key('\\');            // 、
    paired.key('^');             // ……
    check_eq(paired.preedit(), "（你）『好』！：、……",
             "fullwidth paired punctuation, dunhao and ellipsis");

    Sim symbols;
    symbols.b.setFullWidthPunct(true);
    symbols.key('@');
    symbols.key('#');
    symbols.key('$');
    symbols.key('%');
    symbols.key('&');
    symbols.key('*');
    symbols.key('+');
    symbols.key('=');
    symbols.key('|');
    symbols.key('~');
    symbols.key('_');
    symbols.key('`');
    symbols.key('"');
    check_eq(symbols.preedit(), "＠＃＄％＆＊＋＝｜～＿｀＂",
             "fullwidth common ASCII symbols");

    Sim e;
    e.b.setFullWidthPunct(true);
    e.type("API");
    e.key('?');
    check_eq(e.preedit(), "API？",
             "fullwidth punctuation also applies after English token");

    Sim peel;
    peel.b.setFullWidthPunct(true);
    peel.type("aceru/6aj4");
    check_eq(peel.preedit(), "acer螢幕",
             "fullwidth mode keeps zhuyin tail peeling after English");

    Sim forced;
    forced.b.setFullWidthPunct(true);
    forced.press(fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl));
    forced.type("API");
    forced.key('?');
    check_eq(forced.preedit(), "API?",
             "forced English keeps punctuation literal");

    Sim kp;
    kp.b.setFullWidthPunct(true);
    kp.type("API");
    kp.key(FcitxKey_KP_Decimal);
    check_eq(kp.preedit(), "API.",
             "keypad punctuation stays literal in full-width mode");

    // ',' must remain bopomofo even in full-width mode (謝/些 need ㄝ).
    Sim g;
    g.b.setFullWidthPunct(true);
    g.type("xu,4");           // a syllable using ',' = ㄝ
    check(g.preedit() != "x，4" && !g.preedit().empty(),
          "',' stays bopomofo in full-width mode");

    if (ari_ime::keyboardLayoutAvailable(ari_ime::KeyboardLayout::Hsu)) {
        Sim hsu;
        ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Hsu);
        hsu.b.setKeyboardLayout(ari_ime::KeyboardLayout::Hsu);
        hsu.b.setFullWidthPunct(true);
        hsu.type("nefhwf"); // 許氏: f is both ㄈ and contextual ˇ.
        hsu.key('?');
        check_eq(hsu.preedit(), "你好？",
                 "fullwidth mode keeps Hsu dual-role tone keys as bopomofo");
    }

    if (ari_ime::keyboardLayoutAvailable(ari_ime::KeyboardLayout::GinYieh)) {
        Sim ginYieh;
        ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::GinYieh);
        ginYieh.b.setKeyboardLayout(ari_ime::KeyboardLayout::GinYieh);
        ginYieh.b.setFullWidthPunct(true);
        ginYieh.type("d-a"); // 精業: '-' is part of the ㄋㄧˇ key sequence.
        ginYieh.key('?');
        check_eq(ginYieh.preedit(), "你？",
                 "fullwidth mode keeps symbol-looking layout keys as bopomofo");
    }

    if (ari_ime::keyboardLayoutAvailable(ari_ime::KeyboardLayout::Ibm)) {
        Sim ibm;
        ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Ibm);
        ibm.b.setKeyboardLayout(ari_ime::KeyboardLayout::Ibm);
        ibm.b.setFullWidthPunct(true);
        ibm.type("7a,-;,"); // IBM uses ',', '-' and ';' as layout keys.
        ibm.key('?');
        check_eq(ibm.preedit(), "你好？",
                 "fullwidth mode keeps IBM punctuation-looking keys as bopomofo");
    }

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

void test_ambiguous_symbol_boundary_literals() {
    if (!ari_ime::keyboardLayoutAvailable(ari_ime::KeyboardLayout::GinYieh)) {
        ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
        return;
    }
    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::GinYieh);

    Sim start;
    start.b.setKeyboardLayout(ari_ime::KeyboardLayout::GinYieh);
    start.key('-'); // 精業: '-' is a valid zhuyin key and now stays pending first.
    check_eq(start.preedit(), "-", "boundary symbol-like zhuyin key stays pending at start");
    start.key(FcitxKey_space);
    check(start.preedit() != "- ",
          "complete tone-1 symbol-led syllable converts before falling back to literal");

    Sim afterSpace;
    afterSpace.b.setKeyboardLayout(ari_ime::KeyboardLayout::GinYieh);
    afterSpace.key(FcitxKey_space);
    afterSpace.key('-');
    check_eq(afterSpace.preedit(), " -",
             "a symbol-like zhuyin key still shows raw while the syllable is incomplete");

    Sim symbolHeavy;
    symbolHeavy.b.setKeyboardLayout(ari_ime::KeyboardLayout::GinYieh);
    symbolHeavy.key('-');
    symbolHeavy.key('?');
    check_eq(symbolHeavy.preedit(), "-?",
             "invalid symbol-heavy sequence still falls back to literal");

    // The point here is that `-`, a punctuation-looking key, is consumed as part
    // of the syllable instead of leaking out as a literal. Which characters the
    // two syllables become is libchewing's call and differs between releases,
    // so derive that half of the expectation.
    Sim chinese;
    chinese.b.setKeyboardLayout(ari_ime::KeyboardLayout::GinYieh);
    chinese.type("d-a");
    chinese.type("vla");
    const std::string typed = chinese.preedit();
    check(typed.find('-') == std::string::npos,
          "a punctuation-looking zhuyin key does not leak into the preedit");
    check(utf8_count(typed) == 2,
          "two symbol-containing syllables convert to two characters");
    const std::string wantTyped =
        direct_conversion(ari_ime::KeyboardLayout::GinYieh, "d-avla");
    if (!wantTyped.empty()) {
        check_eq(typed, wantTyped,
                 "normal zhuyin typing still works with punctuation-looking keys inside a syllable");
    }

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);

    if (ari_ime::isValidSyllable(",3-3", /*allowTone=*/true)) {
        Sim afterChinese;
        afterChinese.b.setKeyboardLayout(ari_ime::KeyboardLayout::Default);
        afterChinese.type("su3");
        afterChinese.type(",3-3");
        check(afterChinese.preedit() != "你,3-3",
              "complete symbol-led syllable converts after Chinese text");

        Sim atStart;
        atStart.b.setKeyboardLayout(ari_ime::KeyboardLayout::Default);
        atStart.type(",3-3");
        check(atStart.preedit() != ",3-3",
              "complete symbol-led syllable converts at start");
    }

    if (ari_ime::isValidSyllable(".3-3", /*allowTone=*/true)) {
        Sim dotted;
        dotted.b.setKeyboardLayout(ari_ime::KeyboardLayout::Default);
        dotted.type(".3-3");
        check(dotted.preedit() != ".3-3",
              "dot-led zhuyin is preferred over punctuation-heavy literal input");
    }

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Ibm);

    Sim ibmRecovered;
    ibmRecovered.b.setKeyboardLayout(ari_ime::KeyboardLayout::Ibm);
    ibmRecovered.type("7a,-;,");
    check_eq(ibmRecovered.preedit(), "你好",
             "IBM symbol-led syllable still recovers after literal staging");

    SymbolLeadCase paren{};
    if (find_symbol_lead_case('(', paren)) {
        Sim recovered;
        recovered.b.setKeyboardLayout(paren.layout);
        recovered.type(paren.keys);
        check(recovered.preedit() != paren.keys,
              "clear symbol-led zhuyin composition can still convert after boundary literal preference");
    }

    SymbolLeadCase closeParen{};
    if (find_symbol_lead_case(')', closeParen)) {
        Sim recovered;
        recovered.b.setKeyboardLayout(closeParen.layout);
        recovered.type(closeParen.keys);
        check(recovered.preedit() != closeParen.keys,
              "clear close-paren-led zhuyin composition can still convert after boundary literal preference");
    }

    ari_ime::setCurrentKeyboardLayout(ari_ime::KeyboardLayout::Default);
}

void test_deterministic_key_stress() {
    struct Event {
        fcitx::Key key;
    };
    const Event events[] = {
        {fcitx::Key(FcitxKey_s)},      {fcitx::Key(FcitxKey_u)},
        {fcitx::Key(FcitxKey_3)},      {fcitx::Key(FcitxKey_c)},
        {fcitx::Key(FcitxKey_l)},      {fcitx::Key(FcitxKey_g)},
        {fcitx::Key(FcitxKey_4)},      {fcitx::Key(FcitxKey_j)},
        {fcitx::Key(FcitxKey_i)},      {fcitx::Key(FcitxKey_1)},
        {fcitx::Key(FcitxKey_period)}, {fcitx::Key(FcitxKey_slash)},
        {fcitx::Key(FcitxKey_less)},   {fcitx::Key(FcitxKey_greater)},
        {fcitx::Key(FcitxKey_question)},
        {fcitx::Key(FcitxKey_bracketleft)},
        {fcitx::Key(FcitxKey_bracketright)},
        {fcitx::Key(FcitxKey_parenleft)},
        {fcitx::Key(FcitxKey_parenright)},
        {fcitx::Key(FcitxKey_exclam)},
        {fcitx::Key(FcitxKey_colon)},
        {fcitx::Key(FcitxKey_space)},  {fcitx::Key(FcitxKey_Return)},
        {fcitx::Key(FcitxKey_BackSpace)},
        {fcitx::Key(FcitxKey_Delete)}, {fcitx::Key(FcitxKey_Left)},
        {fcitx::Key(FcitxKey_Right)},  {fcitx::Key(FcitxKey_Up)},
        {fcitx::Key(FcitxKey_Down)},   {fcitx::Key(FcitxKey_Home)},
        {fcitx::Key(FcitxKey_End)},    {fcitx::Key(FcitxKey_Page_Up)},
        {fcitx::Key(FcitxKey_Page_Down)},
        {fcitx::Key(FcitxKey_Escape)}, {fcitx::Key(FcitxKey_Tab)},
        {fcitx::Key(FcitxKey_space, fcitx::KeyState::Ctrl)},
    };

    unsigned int seed = 0xA11E2026u;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };

    for (int seq = 0; seq < 48; ++seq) {
        Sim s;
        if ((seq % 3) == 1) {
            s.b.setFullWidthPunct(true);
        }
        for (int step = 0; step < 96; ++step) {
            const Event &event = events[next() % (sizeof(events) / sizeof(events[0]))];
            s.press(event.key);
            check_invariants(s, "deterministic key stress invariant");
        }
    }
}

} // namespace

int main() {
    // Isolate chewing's learned dictionary in a fresh temp dir: tests stay
    // deterministic (no leaked homophone frequencies between runs) and never
    // pollute the user's real ~/.config/inputer dictionary. Must happen before
    // any Buffer/Zhuyin is constructed, since the path is read at chewing_new2.
    test::TempConfigHome configHome("inputer-buffer-test-config");

    test_typing();
    test_input_error_correction();
    test_tone1_space_uses_conversion_result();
    test_common_mixed_literals();
    test_local_context_prediction_examples();
    test_eten_typing();
    test_hsu_typing();
    test_additional_layout_typing();
    test_layout_switch_resets_preedit();
    test_keypad_literal();
    test_keypad_navigation();
    test_enter_commit();
    test_forced_english_toggle();
    test_forced_english_persists_across_reset();
    test_forced_english_caret_editing();
    test_backspace();
    test_phrase_priority();
    test_forward_phrase_recommendation();
    test_live_candidate_preview();
    test_live_matches_top_candidate();
    test_reconversion_core();
    test_phrase_pick();
    test_pick_leaves_caret_after_correction();
    test_candidate_direct_selection();
    test_stale_candidate_activation_is_ignored();
    test_pin_earlier_pick();
    test_candidate_ranking_prefers_current_choice();
    test_candidate_selection_undo();
    test_symbol_heavy_context_keeps_chinese_first();
    test_insert_chinese_midstring();
    test_paste_at_caret();
    test_paste_caret_with_multi_codepoint_cells();
    test_paste_grapheme_editing();
    test_normal_caret_counts_graphemes();
    test_midstring_delete_boundaries();
    test_up_navigates_not_revert();
    test_revert_entry();
    test_candidate_paging();
    test_up_shows_bopomofo_symbol();
    test_candidate_arrow_key_and_caret_options();
    test_candidate_tab_navigation();
    test_reinterpret();
    test_insert_while_selecting();
    test_commit_after_pick();
    test_selection_backspace();
    test_caret_delete_home_end();
    test_phrase_cursor_navigation();
    test_long_chinese_candidate_window_alignment();
    test_direct_navigation_enters_editing();
    test_escape_behavior();
    test_candidate_control_closes_to_caret();
    test_candidate_modifier_keeps_window();
    test_candidate_right_reaches_end();
    test_picking_delete_focused_cell();
    test_fullwidth_punct();
    test_ambiguous_symbol_boundary_literals();
    test_deterministic_key_stress();

    {
        test::TempConfigHome learningHome("inputer-preference-update", false);
        Zhuyin first;
        Zhuyin second;
        check(first.ok() && second.ok(), "two learning contexts can open");
        check(first.rememberPreferredPhrase("妳"),
              "first context records its deliberate choice");
        check(second.rememberPreferredPhrase("您好"),
              "second context records its deliberate choice");
        std::ifstream before(ari_ime::userPreferencePath());
        const std::string both((std::istreambuf_iterator<char>(before)),
                               std::istreambuf_iterator<char>());
        check(both.find("妳\n") != std::string::npos &&
                  both.find("您好\n") != std::string::npos,
              "stale context preserves another context's preference");
        check(first.forgetUserPhrase("妳") > 0,
              "first context forgets its own preference");
        std::ifstream after(ari_ime::userPreferencePath());
        const std::string remaining((std::istreambuf_iterator<char>(after)),
                                    std::istreambuf_iterator<char>());
        check(remaining.find("妳\n") == std::string::npos &&
                  remaining.find("您好\n") != std::string::npos,
              "forgetting preserves another context's preference");
    }

    return test::finish();
}
