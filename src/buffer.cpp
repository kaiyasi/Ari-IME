// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "buffer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include <fcitx-utils/keysym.h>

#include "constants.h"
#include "layout.h"
#include "unicode.h"

namespace {

constexpr int kPunctuationCandidateDown = -2;

// Numeric-keypad keys (NumLock on) arrive as KP_* keysyms instead of the ASCII
// sym of the equivalent main-row key. Map them back to ASCII so they flow
// through the same handleChar path; otherwise they bypass the engine and the
// application inserts them directly, scrambling order against text still held
// in our pre-edit (e.g. typing 2~3 on the keypad produced 23~).
// Number of Unicode characters in a UTF-8 string (counts lead bytes).
int utf8Count(const std::string &s) {
    return ari_ime::unicode::graphemeCount(s);
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
    return isAsciiControl(ch) || ch == "\r\n" || ch == "\xc2\xa0" ||
           ch == "\xe2\x80\xa8" ||
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

// Candidates shown when the user re-opens a literal punctuation cell. This is
// a recognition catalog, not the list displayed in the candidate window. It
// deliberately stays broad so punctuation inserted by an older version can
// still be re-opened; the displayed candidates are derived from the physical
// key below.
const std::vector<std::string> &punctuationCandidateCatalog() {
    static const std::vector<std::string> kCandidates{
        ",",  "，", "、", ".",  "。", "．", "!",  "！", "?",  "？",
        ";",  "；", ":",  "：", "'",  "‘",  "’",  "＇", "\"", "“",
        "”",  "＂", "(",  "（", ")",  "）", "[",  "「", "{",  "『",
        "【",  "〔",  "〈",  "《", "]",  "」", "}",  "』", "】",  "〕",
        "〉",  "》", "<",  "＜", ">",  "＞", "/",  "／", "\\", "＼",
        "-",  "－", "–",  "—", "―", "_",  "＿", "^",  "＾", "…",  "……",
        "@",  "＠", "#",  "＃", "$",  "＄", "%",  "％", "&",  "＆",
        "*",  "＊", "+",  "＋", "=",  "＝", "|",  "｜", "~",  "～",
        "`",  "｀", "·",  "・", "•",  "※", "〝", "〞", "〟", "﹁",  "﹂",
        "﹃",  "﹄", "〜",  "￣"};
    return kCandidates;
}

// The candidate list follows the physical key, rather than grouping symbols
// by visual similarity. For example `<` belongs to the comma key and `>` to
// the period key; `[` must never inherit the candidates of `<` or `!`.
struct PhysicalPunctuationKey {
    char base;
    char shifted;
};

constexpr std::array<PhysicalPunctuationKey, 21> kPhysicalPunctuationKeys{{
    {'1', '!'},  {'2', '@'},  {'3', '#'},  {'4', '$'},  {'5', '%'},
    {'6', '^'},  {'7', '&'},  {'8', '*'},  {'9', '('},  {'0', ')'},
    {'-', '_'},  {'=', '+'},  {'`', '~'},  {'[', '{'},  {']', '}'},
    {';', ':'}, {'\'', '"'}, {'\\', '|'}, {',', '<'}, {'.', '>'},
    {'/', '?'},
}};

bool physicalPunctuationKeyForChar(char c, PhysicalPunctuationKey &result) {
    for (const auto &key : kPhysicalPunctuationKeys) {
        if (key.base == c || key.shifted == c) {
            result = key;
            return true;
        }
    }
    return false;
}

bool isPunctuationText(const std::string &text) {
    if (text.size() == 1 &&
        std::ispunct(static_cast<unsigned char>(text.front()))) {
        return true;
    }
    const auto &catalog = punctuationCandidateCatalog();
    return std::find(catalog.begin(), catalog.end(), text) != catalog.end();
}

bool containsHanCharacter(const std::string &text) {
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

// Some frontends expose a shifted number-row key as its resulting punctuation
// keysym (`$` instead of `4`). Keep the mapping local to tone handling; a dollar
// sign in ordinary English must remain a dollar sign when no Chinese
// composition is active.
char shiftedNumberRowBase(fcitx::KeySym sym) {
    switch (sym) {
    case '!': return '1';
    case '@': return '2';
    case '#': return '3';
    case '$': return '4';
    case '%': return '5';
    case '^': return '6';
    case '&': return '7';
    case '*': return '8';
    case '(': return '9';
    case ')': return '0';
    default: return 0;
    }
}

char preserveTypedAsciiCase(const fcitx::Key &key, fcitx::KeySym sym) {
    if (sym < 33 || sym > 126) {
        return 0;
    }
    char c = static_cast<char>(sym);
    // Fcitx frontends may report Shift+i as either the uppercase keysym or the
    // lowercase keysym with a Shift state. Keep the latter form uppercase too,
    // so raw-key recovery can reproduce what the user actually typed.
    if (key.states().test(fcitx::KeyState::Shift) && c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - ('a' - 'A'));
    }
    return c;
}

bool isModifierOnlyKeySym(fcitx::KeySym sym) {
    // X11 modifier keysyms. Keep this local and numeric because the headless
    // WASM key shim intentionally exposes only the small API used by Buffer.
    switch (sym) {
    case 0xffe1: // Shift_L
    case 0xffe2: // Shift_R
    case 0xffe3: // Control_L
    case 0xffe4: // Control_R
    case 0xffe5: // Caps_Lock
    case 0xffe6: // Shift_Lock
    case 0xffe7: // Meta_L
    case 0xffe8: // Meta_R
    case 0xffe9: // Alt_L
    case 0xffea: // Alt_R
    case 0xffeb: // Super_L
    case 0xffec: // Super_R
    case 0xffed: // Hyper_L
    case 0xffee: // Hyper_R
        return true;
    default:
        return false;
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

bool isShiftedAsciiPunctuation(fcitx::KeySym sym) {
    if (sym < 33 || sym > 126) {
        return false;
    }
    constexpr std::string_view shifted = R"(~!@#$%^&*()_+{}|:"<>?)";
    return shifted.find(static_cast<char>(sym)) != std::string_view::npos;
}

bool punctuationShortcutActive(
    const fcitx::Key &key, ari_ime::ChinesePunctuationShortcut shortcut) {
    const bool ctrl = key.states().test(fcitx::KeyState::Ctrl);
    const bool alt = key.states().test(fcitx::KeyState::Alt);
    const bool super = key.states().test(fcitx::KeyState::Super);
    const bool shifted = key.states().test(fcitx::KeyState::Shift) ||
                         isShiftedAsciiPunctuation(key.sym());
    if (super || (ctrl && alt)) {
        return false;
    }
    switch (shortcut) {
    case ari_ime::ChinesePunctuationShortcut::ControlShift:
        return ctrl && shifted;
    case ari_ime::ChinesePunctuationShortcut::AltShift:
        return alt && shifted;
    case ari_ime::ChinesePunctuationShortcut::Control:
        return ctrl;
    case ari_ime::ChinesePunctuationShortcut::Alt:
        return alt;
    case ari_ime::ChinesePunctuationShortcut::Disabled:
        return false;
    }
    return false;
}

// Reserve the two bracket keys as a small, predictable Chinese-typing
// convenience even though ordinary Alt punctuation remains available to the
// application.  The unshifted physical keys produce the Chinese corner quote
// pair: Alt+[ -> 「 and Alt+] -> 」.
bool isAltCornerQuoteKey(const fcitx::Key &key, fcitx::KeySym sym) {
    if (!key.states().test(fcitx::KeyState::Alt) ||
        key.states().testAny(fcitx::KeyStates{fcitx::KeyState::Ctrl,
                                              fcitx::KeyState::Shift,
                                              fcitx::KeyState::Super})) {
        return false;
    }
    return sym == FcitxKey_bracketleft || sym == FcitxKey_bracketright;
}

Zhuyin *probeForCurrentLayout(Zhuyin *&probe,
                              ari_ime::KeyboardLayout &probeLayout) {
    ari_ime::KeyboardLayout layout = ari_ime::currentKeyboardLayout();
    if (!probe || probeLayout != layout) {
        delete probe;
        probe = new Zhuyin();
        probeLayout = layout;
    }
    return probe;
}

// The Bopomofo symbol(s) libchewing maps `keys` to on the current layout, or
// empty when they are not 注音 keys. Tone keys map to their mark (3 -> ˇ), so
// callers that want a syllable body must strip tones themselves.
std::string bopomofoForKeys(const std::string &keys) {
    // Intentionally leaked at process exit, same rationale as syllableConverts
    // below: rebuilt only on explicit layout changes.
    static Zhuyin *probe = nullptr;
    static ari_ime::KeyboardLayout probeLayout = ari_ime::KeyboardLayout::Default;
    Zhuyin *ctx = probeForCurrentLayout(probe, probeLayout);
    ctx->feedSequence(ari_ime::canonicalKeys(keys));
    return ctx->bopomofoString();
}

// Whether a complete (toned) canonical syllable converts to a Chinese character
// with nothing left dangling.
bool syllableConverts(const std::string &canonicalKeys) {
    // Intentionally leaked at process exit: a chewing context must not be torn
    // down during static destruction. It is rebuilt only on explicit layout
    // changes while the process is alive.
    static Zhuyin *probe = nullptr;
    static ari_ime::KeyboardLayout probeLayout = ari_ime::KeyboardLayout::Default;
    probe = probeForCurrentLayout(probe, probeLayout);
    probe->feedSequence(canonicalKeys);
    return probe->hasConverted() && !probe->hasBopomofo() &&
           containsHanCharacter(probe->preedit());
}

// Whether a bopomofo body (no tone) yields a character under 一聲 (the tone the
// space key applies). Used to decide if space should convert a pending syllable.
bool syllableConvertsTone1(const std::string &canonicalBody) {
    static Zhuyin *probe = nullptr;
    static ari_ime::KeyboardLayout probeLayout = ari_ime::KeyboardLayout::Default;
    probe = probeForCurrentLayout(probe, probeLayout);
    probe->feedSequence(canonicalBody);
    probe->handleSpace(); // 一聲
    return probe->hasConverted() && !probe->hasBopomofo() &&
           containsHanCharacter(probe->preedit());
}

// chewing natively maps the shifted / standalone punctuation keys to full-width
// Chinese punctuation (e.g. '<' -> ，, '>' -> 。, '?' -> ？, '[' -> 「,
// '\'' -> 、). Return that mapping for key `c`, or empty for letters/digits/
// unmapped keys (so they keep the literal English path). The 注音 韻母 keys
// ', . ; / -' never reach here — they form bopomofo (ㄝㄡㄤㄥㄦ) and must not be
// repurposed.
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
    default: break;
    }
    // Intentionally leaked, like the syllable probes above.
    static Zhuyin *probe = nullptr;
    static ari_ime::KeyboardLayout probeLayout = ari_ime::KeyboardLayout::Default;
    probe = probeForCurrentLayout(probe, probeLayout);
    probe->resetAll();
    probe->handleDefault(static_cast<int>(c));
    // A key that is a 注音 key in this layout parks a Bopomofo symbol instead
    // of producing punctuation; never let that glyph leak out as "punct".
    if (probe->hasBopomofo()) {
        probe->resetAll();
        return {};
    }
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

// The unshifted punctuation-looking keys are also valid 注音 keys in 大千 and
// several other layouts. Their ordinary full-width path must leave them
// available for syllables, but an explicit punctuation shortcut deliberately
// asks for the punctuation associated with that physical key.
std::string chinesePunctShortcut(char c) {
    switch (c) {
    case ',': return "，";
    case '.': return "。";
    case '/': return "？";
    case ';': return "；";
    case '-': return "－";
    default: return chinesePunct(c);
    }
}

std::string punctuationForShortcutEvent(
    char sym, fcitx::KeyStates states,
    ari_ime::ChinesePunctuationShortcut shortcut) {
    const auto keySym = static_cast<fcitx::KeySym>(sym);
    const fcitx::Key key(keySym, states);
    if (isAltCornerQuoteKey(key, keySym)) {
        return sym == FcitxKey_bracketleft ? "「" : "」";
    }
    if (!punctuationShortcutActive(key, shortcut)) {
        return {};
    }
    if (sym == FcitxKey_apostrophe || sym == FcitxKey_quotedbl) {
        return "、";
    }
    return chinesePunctShortcut(sym);
}

void appendUniquePunctuation(std::vector<std::string> &candidates,
                             std::string candidate) {
    if (candidate.empty() ||
        std::find(candidates.begin(), candidates.end(), candidate) !=
            candidates.end()) {
        return;
    }
    candidates.push_back(std::move(candidate));
}

// Enumerate the outputs that Ari can associate with one physical key. Raw
// base/Shift symbols are always present. The Chinese forms come from the
// same paths as handleAuto: full-width mode, Ctrl/Shift, Alt/Shift and the
// reserved Alt corner quotes. We probe every supported shortcut policy here
// because the candidate window is also the direct way to choose a form when
// the user's current shortcut binding is different.
std::vector<std::string> punctuationCandidatesForPhysicalKey(
    const PhysicalPunctuationKey &key, bool fullWidthPunctuation) {
    std::vector<std::string> candidates;
    appendUniquePunctuation(candidates, std::string(1, key.base));
    appendUniquePunctuation(candidates, std::string(1, key.shifted));

    if (fullWidthPunctuation) {
        for (char sym : {key.base, key.shifted}) {
            if (ari_ime::zhuyinSlot(sym) < 0) {
                appendUniquePunctuation(candidates, chinesePunct(sym));
            }
        }
    }

    const fcitx::KeyStates ctrl{fcitx::KeyState::Ctrl};
    const fcitx::KeyStates ctrlShift{fcitx::KeyState::Ctrl,
                                     fcitx::KeyState::Shift};
    const fcitx::KeyStates alt{fcitx::KeyState::Alt};
    const fcitx::KeyStates altShift{fcitx::KeyState::Alt,
                                    fcitx::KeyState::Shift};

    // These are the four user-selectable shortcut shapes. The bracket
    // convenience is added with Disabled as well, because it is reserved
    // independently of ChinesePunctuationShortcut.
    appendUniquePunctuation(
        candidates,
        punctuationForShortcutEvent(
            key.base, ctrl, ari_ime::ChinesePunctuationShortcut::Control));
    appendUniquePunctuation(
        candidates,
        punctuationForShortcutEvent(
            key.shifted, ctrlShift,
            ari_ime::ChinesePunctuationShortcut::ControlShift));
    appendUniquePunctuation(
        candidates,
        punctuationForShortcutEvent(
            key.base, alt, ari_ime::ChinesePunctuationShortcut::Disabled));
    appendUniquePunctuation(
        candidates,
        punctuationForShortcutEvent(
            key.base, alt, ari_ime::ChinesePunctuationShortcut::Alt));
    appendUniquePunctuation(
        candidates,
        punctuationForShortcutEvent(
            key.shifted, altShift,
            ari_ime::ChinesePunctuationShortcut::AltShift));

    // The Chinese bracket family belongs to the two bracket keys, the same
    // grouping the recognition catalog uses. Nested title marks 《》 and
    // tortoise-shell 【】 stay reachable without borrowing unrelated symbols
    // from non-bracket keys.
    if (key.base == '[' || key.base == ']') {
        const std::array<const char *, 4> &marks =
            key.base == '[' ? std::array<const char *, 4>{"【", "〔", "《", "〈"}
                            : std::array<const char *, 4>{"】", "〕", "》", "〉"};
        for (const char *mark : marks) {
            appendUniquePunctuation(candidates, mark);
        }
    }

    return candidates;
}

bool physicalPunctuationKeyForText(std::string_view text,
                                   PhysicalPunctuationKey &result) {
    if (text.size() == 1 &&
        physicalPunctuationKeyForChar(text.front(), result)) {
        return true;
    }

    // A Chinese punctuation character no longer carries the physical key it
    // came from in Cell. Reconstruct the most specific family from the same
    // reachable-output table so reopening `「` still shows `[`, `{`, `「` and
    // `『`, while a bracket never gains unrelated symbols.
    for (const auto &key : kPhysicalPunctuationKeys) {
        const auto candidates =
            punctuationCandidatesForPhysicalKey(key, /*fullWidth=*/true);
        if (std::find(candidates.begin(), candidates.end(), text) !=
            candidates.end()) {
            result = key;
            return true;
        }
    }
    return false;
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

// Same split for the keys as actually typed (Cell::typed), preferring it over
// the canonical reading when a typed record was kept.
std::pair<std::string, bool> typedBody(const std::string &typed,
                                       const std::string &reading) {
    return readingBody(typed.empty() ? reading : typed);
}

bool isAsciiLower(char c) {
    return c >= 'a' && c <= 'z';
}

bool hasAsciiLetter(const std::string &s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    });
}

bool sameAsciiKey(char a, char b) {
    if (a >= 'A' && a <= 'Z') {
        a = static_cast<char>(a + ('a' - 'A'));
    }
    if (b >= 'A' && b <= 'Z') {
        b = static_cast<char>(b + ('a' - 'A'));
    }
    return a == b;
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
        int slot = ari_ime::zhuyinSlot(c);
        if (slot < 0 || slot == ari_ime::kToneSlot) {
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
    // A lowercase initial may legitimately start the Chinese suffix even when
    // the English prefix also ends in a lowercase letter. The reverse search
    // already prefers the shortest valid trailing syllable, so rejecting this
    // boundary would hide valid initial+final syllables without preventing a
    // meaningful ambiguity.
    return true;
}

bool canPeelSymbolLedBody(const std::string &body) {
    if (body.size() < 2 || !ari_ime::isSymbolLikeZhuyinKey(body.front())) {
        return false;
    }
    return std::any_of(body.begin() + 1, body.end(), [](char c) {
        return ari_ime::zhuyinSlot(c) >= 0;
    });
}

bool isAsciiWord(const std::string &s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(),
                       [](char c) { return (c >= 'A' && c <= 'Z') ||
                                           (c >= 'a' && c <= 'z'); });
}

bool canPeelSymbolLedFromEnglish(const std::string &prefix,
                                 const std::string &body) {
    if (!canPeelSymbolLedBody(body)) {
        return false;
    }
    // Symbol-led syllables are valid mixed-input Chinese, but in technical
    // literals (URLs, filenames, versions, identifiers) the same suffixes are
    // much more likely to be punctuation. Only peel them out of a plain English
    // word tail, or from the start of the token.
    return prefix.empty() || isAsciiWord(prefix);
}

bool hasAsciiDigit(const std::string &s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        return c >= '0' && c <= '9';
    });
}

} // namespace

std::string Buffer::pendingSyllableHint() const {
    if (syl_.empty() || !zhuyin_.ok()) {
        return {};
    }
    // Tone keys are stripped: chewing drops a lone initial + tone outright,
    // so only the body can be probed while the syllable is incomplete.
    std::string body;
    for (char c : syl_) {
        if (!ari_ime::isToneKey(c)) {
            body.push_back(c);
        }
    }
    if (body.empty()) {
        return {};
    }
    return bopomofoForKeys(body);
}

void Buffer::reset() {
    token_ = Token::Chinese;
    cells_.clear();
    tail_.clear();
    runReadings_.clear();
    runTyped_.clear();
    englishBuf_.clear();
    invalidPrefixLength_ = 0;
    invalidSequenceLength_ = 0;
    duplicateKey_ = 0;
    syl_.clear();
    selecting_ = false;
    candOpen_ = false;
    caretPos_ = 0;
    selCursor_ = 0;
    highlight_ = 0;
    selCands_.clear();
    selPage_ = 0;
    runLoaded_ = false;
    nextSelectionGroup_ = 1;
    selectionUndo_.clear();
    zhuyin_.resetAll();
}

bool Buffer::setKeyboardLayout(ari_ime::KeyboardLayout layout) {
    if (layout_ == layout) {
        return false;
    }
    // Readings stored in existing cells are layout-specific raw keys. If the
    // user changes layouts mid-preedit, keep the invariant simple and avoid
    // re-feeding old readings through a different layout.
    reset();
    layout_ = layout;
    ari_ime::setCurrentKeyboardLayout(layout_);
    zhuyin_.setKeyboardLayout(layout_);
    // knownReadings_ caches the same layout-specific raw keys for
    // reconversion; stale entries would be re-fed through the new layout and
    // produce garbage readings.
    knownReadings_.clear();
    knownReadingsOrder_.clear();
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
        std::string typed = i < runTyped_.size() ? runTyped_[i] : "";
        cells_.push_back({true, chars[i], reading, std::move(typed)});
    }
    std::size_t n = std::min(chars.size(), runReadings_.size());
    runReadings_.erase(runReadings_.begin(), runReadings_.begin() + n);
    runTyped_.erase(runTyped_.begin(), runTyped_.begin() + n);
}

void Buffer::freezeRun() {
    auto chars = splitUtf8(zhuyin_.preedit());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        std::string reading = i < runReadings_.size() ? runReadings_[i] : "";
        std::string typed = i < runTyped_.size() ? runTyped_[i] : "";
        cells_.push_back({true, chars[i], reading, std::move(typed)});
    }
    runReadings_.clear();
    runTyped_.clear();
    zhuyin_.resetAll();
}

void Buffer::freezeEnglish() {
    for (char c : englishBuf_) {
        cells_.push_back({false, std::string(1, c), {}});
    }
    englishBuf_.clear();
    invalidPrefixLength_ = 0;
    invalidSequenceLength_ = 0;
    duplicateKey_ = 0;
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

KeyResult Buffer::beginReconversion(const std::string &text) {
    if (text.empty() || ari_ime::unicode::graphemeCount(text) >
                            ari_ime::kMaxCompositionChars ||
        !preeditText().empty()) {
        return {false, false, {}, false};
    }

    const auto chars = ari_ime::unicode::splitGraphemes(text);
    if (chars.empty() ||
        std::any_of(chars.begin(), chars.end(), [](const std::string &character) {
            return !containsHanCharacter(character);
        })) {
        return {false, false, {}, false};
    }
    std::vector<std::string> readings(chars.size());
    std::size_t missing = 0;
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const auto known = knownReadings_.find(chars[i]);
        if (known != knownReadings_.end()) {
            readings[i] = known->second;
        } else {
            ++missing;
        }
    }
    if (missing > 0) {
        const auto reverse = zhuyin_.readingsForText(text);
        for (std::size_t i = 0; i < chars.size(); ++i) {
            if (readings[i].empty() && i < reverse.size()) {
                readings[i] = reverse[i];
            }
        }
    }
    if (readings.size() != chars.size() ||
        std::any_of(readings.begin(), readings.end(),
                    [](const std::string &reading) { return reading.empty(); })) {
        return {false, false, {}, false};
    }

    reset();
    cells_.reserve(chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        // The reverse lookup already gives the selected text's readings. Leave
        // the cells unlocked so libchewing can still expose phrase-level
        // alternatives instead of treating every character as a hard pick.
        cells_.push_back({true, chars[i], readings[i]});
    }
    selecting_ = true;
    candOpen_ = false;
    caretPos_ = 0;
    return handleSelecting(fcitx::Key(FcitxKey_Down));
}

std::vector<std::string> Buffer::candidates() const {
    // The current page (9) of the merged phrase+single or punctuation candidate
    // list. Ordinary English cells remain cursor-only with nothing to pick.
    std::vector<std::string> out;
    int count = visibleCandidateCount();
    if (count <= 0) {
        return out; // caret mode shows no candidate window
    }
    out.reserve(count);
    int start = selPage_ * ari_ime::kCandPerPage;
    for (int i = start; i < start + count; ++i) {
        out.push_back(selCands_[i].display);
    }
    return out;
}

std::vector<std::string> Buffer::previewCandidates() {
    // A preview is deliberately read-only from the user's point of view. Do
    // not show it while editing a frozen preedit, while a syllable is still
    // ambiguous, or while English is the active tail: in all of those states
    // a candidate panel would make ordinary digits/punctuation look like
    // selection commands.
    if (selecting_ || forcedEnglish_ || !englishBuf_.empty() || !syl_.empty() ||
        !zhuyin_.hasConverted()) {
        return {};
    }

    const std::string live = zhuyin_.preedit();
    if (live.empty()) {
        return {};
    }

    std::vector<std::string> out;
    out.reserve(ari_ime::kCandPerPage);
    // The text already visible in the preedit is the recommendation the user
    // is currently seeing. Keep it first even if a libchewing build exposes a
    // shorter interval when its cursor is parked at the end of a phrase.
    out.push_back(live);

    if (zhuyin_.openCandidates()) {
        const auto page = zhuyin_.pageCandidates();
        zhuyin_.closeCandidates();
        for (const auto &candidate : page) {
            if (candidate.empty() ||
                std::find(out.begin(), out.end(), candidate) != out.end()) {
                continue;
            }
            out.push_back(candidate);
            if (static_cast<int>(out.size()) >= ari_ime::kCandPerPage) {
                break;
            }
        }
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

KeyResult Buffer::selectCandidate(int pageIndex,
                                  const std::string &expectedText) {
    if (!selecting_ || !candOpen_) {
        return {false, false, {}, false};
    }
    if (pageIndex < 0 || pageIndex >= visibleCandidateCount()) {
        return {true, false, {}, false};
    }
    const int globalIndex = selPage_ * ari_ime::kCandPerPage + pageIndex;
    if (globalIndex < 0 || globalIndex >= static_cast<int>(selCands_.size()) ||
        selCands_[globalIndex].display != expectedText) {
        // The frontend may deliver a click after a page/navigation update. It
        // must not reinterpret that old slot against the current page.
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
    return (static_cast<int>(selCands_.size()) + ari_ime::kCandPerPage - 1) /
           ari_ime::kCandPerPage;
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
        // Picking mode parks on the cell being re-picked. In caret mode the
        // insertion point is between cells_, but some literal cells (for
        // example "……") span more than one Unicode codepoint, so sum the real
        // displayed character widths instead of assuming one cell == one char.
        if (candOpen_) {
            return selectionChar();
        }
        int idx = 0;
        for (int i = 0; i < caretPos_ && i < static_cast<int>(cells_.size());
             ++i) {
            idx += utf8Count(cells_[i].text);
        }
        return idx;
    }
    if (tail_.empty()) {
        return -1;
    }
    int before = 0;
    for (const Cell &cell : cells_) {
        before += utf8Count(cell.text);
    }
    before += utf8Count(zhuyin_.preedit())
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
    auto sym = normalizeKeySym(key.sym());

    const bool ctrlOnly = key.states().test(fcitx::KeyState::Ctrl) &&
                          !key.states().testAny(fcitx::KeyStates{
                              fcitx::KeyState::Shift, fcitx::KeyState::Alt,
                              fcitx::KeyState::Super});
    if (sym == FcitxKey_z && ctrlOnly && !selectionUndo_.empty()) {
        return undoSelection();
    }

    const bool ctrlNavigation =
        key.states().test(fcitx::KeyState::Ctrl) &&
        !key.states().testAny(fcitx::KeyStates{
            fcitx::KeyState::Shift, fcitx::KeyState::Alt,
            fcitx::KeyState::Super}) &&
        (sym == FcitxKey_Left || sym == FcitxKey_Right);

    const bool shiftedApostrophe = sym == FcitxKey_apostrophe ||
                                   sym == FcitxKey_quotedbl;
    const bool altCornerQuote = !forcedEnglish_ &&
                                isAltCornerQuoteKey(key, sym);
    const bool explicitChinesePunctuation =
        altCornerQuote ||
        (!forcedEnglish_ && punctuationShortcutActive(key, punctuationShortcut_) &&
         (shiftedApostrophe ||
          (sym >= 33 && sym <= 126 &&
           !chinesePunctShortcut(static_cast<char>(sym)).empty())));

    if (selecting_) {
        if (explicitChinesePunctuation) {
            return beginInsert(candOpen_ ? selCursor_ : caretPos_, key);
        }
        if (hasWordModifier(key) && !ctrlNavigation) {
            return {false, false, {}, false};
        }
        return handleSelecting(key);
    }

    if (ctrlNavigation) {
        return enterSelection(key);
    }

    if (altCornerQuote) {
        clearSelectionUndo();
        freezeAll();
        cells_.push_back(
            {false, sym == FcitxKey_bracketleft ? "「" : "」", {}});
        return {true, false, {}, true};
    }

    // Chinese punctuation is an explicit gesture, independent of surrounding
    // language. The modifier policy is configurable through Fcitx5; some
    // frontends encode Shift only in the resulting keysym, so the shortcut
    // matcher also recognizes shifted ASCII symbols without the Shift bit.
    if (!forcedEnglish_ &&
        punctuationShortcutActive(key, punctuationShortcut_) &&
        shiftedApostrophe) {
        clearSelectionUndo();
        freezeAll();
        cells_.push_back({false, "、", {}});
        return {true, false, {}, true};
    }
    if (!forcedEnglish_ &&
        punctuationShortcutActive(key, punctuationShortcut_) &&
        sym >= 33 && sym <= 126) {
        std::string punct = chinesePunctShortcut(static_cast<char>(sym));
        if (!punct.empty()) {
            clearSelectionUndo();
            freezeAll();
            cells_.push_back({false, punct, {}});
            return {true, false, {}, true};
        }
    }

    // A few frontends pass Shift+4 as the resulting `$` keysym rather than as
    // `4` with a Shift state. In a live Chinese syllable, or while recovering a
    // tagged invalid prefix, interpret that physical number-row key as the
    // layout tone. If it does not complete a Chinese suffix, keep the original
    // punctuation so ordinary dollar signs and similar symbols are unchanged.
    const char shiftedTone = shiftedNumberRowBase(sym);
    const bool shiftedToneContext =
        shiftedTone != 0 && ari_ime::isToneKey(shiftedTone) &&
        !key.states().testAny(fcitx::KeyStates{
            fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
            fcitx::KeyState::Super}) &&
        (key.states().test(fcitx::KeyState::Shift) ||
         invalidPrefixLength_ > 0 || !syl_.empty());
    if (shiftedToneContext) {
        if (token_ == Token::English) {
            KeyResult peeled;
            if (tryPeelEnglish(shiftedTone, peeled)) {
                return peeled;
            }
            englishBuf_.push_back(static_cast<char>(sym));
            return {true, false, {}, true};
        }
        if (!syl_.empty()) {
            return handleChar(shiftedTone);
        }
    }

    if (hasWordModifier(key)) {
        return {false, false, {}, false};
    }

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
        (!forcedEnglish_ || (sym != FcitxKey_Down && sym != FcitxKey_Up))) {
        return enterSelection(key);
    }

    if (int kp = keypadAscii(sym)) {
        return handleChar(static_cast<char>(kp), /*literal=*/true);
    }
    if (sym >= 33 && sym <= 126) {
        return handleChar(preserveTypedAsciiCase(key, sym));
    }

    return {false, false, {}, false};
}

// ---------------------------------------------------------------------------
// Typing path
// ---------------------------------------------------------------------------

KeyResult Buffer::handleChar(char c, bool literal) {
    clearSelectionUndo();
    // Forced pure-English mode, or a failed 注音 engine: everything is literal
    // text in the English tail. Degrading on engine failure keeps keystrokes
    // visible instead of being silently swallowed by no-op chewing calls.
    if (forcedEnglish_ || !zhuyin_.ok()) {
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
        if (fullWidthPunct_ && ari_ime::zhuyinSlot(c) < 0) {
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
        if (ari_ime::isToneKey(c)) {
            KeyResult peeled;
            if (tryPeelEnglish(c, peeled)) {
                return peeled;
            }
        }
        englishBuf_.push_back(c);
        return {true, false, {}, true};
    }

    int s = ari_ime::zhuyinSlot(c);

    // Non-注音 printable (punctuation, uppercase, ...). Ordinary input remains
    // literal regardless of context; only the explicit Ctrl+Shift gesture above
    // or the user-enabled full-width mode converts punctuation.
    if (s < 0) {
        std::string punct = chinesePunct(c);
        if (fullWidthPunct_ && !punct.empty()) {
            freezeAll();
            cells_.push_back({false, punct, {}});
            return {true, false, {}, true};
        }
        return handleLiteralChar(c);
    }

    if (syl_.empty()) {
        if (s == 3) {
            return handleLiteralChar(c); // a tone cannot start a syllable
        }
        syl_.push_back(c);
        return {true, false, {}, true}; // raw until a tone completes it
    }

    // If the extended raw keys cannot be assigned to a valid syllable, the
    // unsealed hypothesis was wrong and we fall back to English. This goes
    // through the layout layer because some layouts have dual-role keys (Hsu
    // 'f' is ㄈ at syllable start but ˇ after a body).
    if (!ari_ime::isValidSyllable(syl_ + c, /*allowTone=*/true)) {
        // Keep the failed hypothesis tagged as a possible accidental prefix.
        // It is only removed later if the remaining English tail independently
        // becomes a complete Chinese syllable; ordinary English remains safe.
        return flipToEnglish(c, /*markInvalidPrefix=*/true);
    }

    syl_.push_back(c);
    // Only turn raw keys into Chinese once they form a complete, toned syllable.
    // A toned-but-incomplete state like "s3" (ㄋˇ) stays raw so an out-of-order
    // vowel can still complete it ("s3" + u -> 你).
    const std::string body = ari_ime::canonicalKeys(syl_);
    const bool needsBody =
        ari_ime::needsBodyBeforeToneCompletion(ari_ime::currentKeyboardLayout());
    if ((!needsBody || ari_ime::hasMedialOrFinal(body)) &&
        syllableConverts(body)) {
        integrateSyllable(body, syl_);
        syl_.clear();
    }
    return {true, false, {}, true};
}

KeyResult Buffer::handleLiteralChar(char c) {
    // Full-width conversion must stay away from keys that are 注音 keys in the
    // active layout: probing them through chewing parks a Bopomofo symbol and
    // force-committing it would emit a stray ㄅㄆㄇ character instead of the
    // literal or full-width form. The other call sites already apply this
    // guard; the keypad and invalid-syllable paths reach here unguarded.
    if (fullWidthPunct_ && ari_ime::zhuyinSlot(c) < 0) {
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

void Buffer::integrateSyllable(const std::string &body,
                               const std::string &typed) {
    if (!englishBuf_.empty()) {
        // English sits in front of this new syllable: it can't merge with the
        // earlier run, so freeze "run + English" into cells_ and start fresh.
        freezeRun();
        freezeEnglish();
        zhuyin_.feedSequence(body);
        runReadings_ = {body};
        runTyped_ = {typed};
    } else if (zhuyin_.hasConverted()) {
        // Extend the live run so chewing's phrasing spans it.
        for (char k : body) {
            zhuyin_.handleDefault(static_cast<int>(k));
        }
        runReadings_.push_back(body);
        runTyped_.push_back(typed);
    } else {
        zhuyin_.feedSequence(body);
        runReadings_ = {body};
        runTyped_ = {typed};
    }
    // Keep libchewing's contextual conversion. On older libchewing releases
    // only, also apply Ari's explicit preference sidecar; newer releases rank
    // those mappings natively and make this call a no-op.
    zhuyin_.promoteUserPhrases();
    moveAutoCommit();
    token_ = Token::Chinese;
}

KeyResult Buffer::flipToEnglish(char trailing, bool markInvalidPrefix) {
    // No commit: the in-progress syllable's raw keys plus the breaking key become
    // the live English tail, sitting after the (still live) chewing run.
    if (markInvalidPrefix && englishBuf_.empty()) {
        // Only a single leading key is a safe implicit-prefix correction. A
        // longer incomplete syllable may be intentional text; repeated-key
        // failures are handled separately using duplicateKey_.
        invalidPrefixLength_ = syl_.size() == 1 ? 1 : 0;
        invalidSequenceLength_ = syl_.size() + 1;
        duplicateKey_ = std::any_of(
            syl_.begin(), syl_.end(), [trailing](char c) {
                return sameAsciiKey(c, trailing);
            })
                            ? trailing
                            : 0;
    } else {
        invalidPrefixLength_ = 0;
        invalidSequenceLength_ = 0;
        duplicateKey_ = 0;
    }
    englishBuf_ += syl_;
    englishBuf_.push_back(trailing);
    syl_.clear();
    token_ = Token::English;
    return {true, false, {}, true};
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
    if (!reading.empty() && ari_ime::isSymbolLikeZhuyinKey(reading.front())) {
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
    const int candidateStart = selRunStart_ + cand.startOffset;
    const int literalBias = literalContextBiasAt(candidateStart);
    const bool hasChineseLeft =
        candidateStart > 0 && cells_[candidateStart - 1].chinese;
    const int len = utf8Count(cand.text);
    const int contextLength = cand.down < 0 ? 1 : len;
    const int candidateEnd = candidateStart + contextLength;
    const bool hasChineseRight =
        candidateEnd < static_cast<int>(cells_.size()) &&
        cells_[candidateEnd].chinese;
    if (cand.down < 0) {
        // "Raw keys" is a recovery path, not a primary interpretation: keep it
        // available everywhere, but behind real Chinese candidates unless the
        // surrounding context is overwhelmingly literal.
        int score = -96 + literalBias * 6;
        if (hasChineseLeft) {
            score -= 18;
        }
        if (hasChineseRight) {
            score -= 18;
        }
        if (literalBias >= 7) {
            score += 14;
        }
        return score;
    }

    int score = 0;
    score += len * 16;
    score -= std::max(cand.down, 0) * 7;

    if (len == 1) {
        score += literalBias * 5;
    } else {
        score -= literalBias * 4;
    }

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
                              static_cast<int>(cells_.size()) - candidateStart);
    for (int i = 0; i < limit; ++i) {
        const Cell &cell = cells_[candidateStart + i];
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
    // stable_sort may allocate a temporary buffer through libstdc++'s
    // deprecated temporary-buffer helpers. With Clang's ASan runtime this can
    // trip alloc-dealloc-mismatch on the candidate path (the allocator pair is
    // selected by the system libstdc++ headers). `order` is already a unique
    // insertion-order tie breaker, so an in-place sort preserves the same
    // ordering without that allocation path.
    std::sort(selCands_.begin(), selCands_.end(),
              [this](const SelCand &a, const SelCand &b) {
                  if (a.down < 0 || b.down < 0) {
                      if (a.down < 0 && b.down < 0) {
                          return a.order < b.order;
                      }
                      return b.down < 0;
                  }
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
    auto commit = [this, &out, tone](const std::string &prefix,
                                     const std::string &body,
                                     const std::string &syllable) {
        // Freeze the live run plus the retained English prefix into cells_, then
        // start a fresh Chinese run with the peeled syllable. The prefix is
        // intentionally empty for a successful typo correction.
        freezeRun();
        for (char c : prefix) {
            cells_.push_back({false, std::string(1, c), {}});
        }
        zhuyin_.feedSequence(syllable);
        runReadings_ = {syllable};
        runTyped_ = {body + std::string(1, tone)};
        zhuyin_.promoteUserPhrases();
        moveAutoCommit();
        token_ = Token::Chinese;
        englishBuf_.clear();
        invalidPrefixLength_ = 0;
        invalidSequenceLength_ = 0;
        duplicateKey_ = 0;
        syl_.clear();
        out = {true, false, {}, true};
        return true;
    };
    auto tryBody = [&commit, tone](const std::string &prefix,
                                   const std::string &body, bool symbolLed) {
        if (!ari_ime::isValidSyllable(body, /*allowTone=*/false)) {
            return false;
        }
        if (symbolLed) {
            if (!canPeelSymbolLedFromEnglish(prefix, body)) {
                return false;
            }
        } else if (!canPeelEnglishBody(prefix, body)) {
            return false;
        }
        std::string syllable = ari_ime::canonicalKeys(body);
        syllable.push_back(tone);
        if (!syllableConverts(syllable)) {
            return false;
        }
        return commit(prefix, body, syllable);
    };
    auto tryCandidate = [&buf, &tryBody](std::size_t split, bool symbolLed) {
        if (split >= buf.size()) {
            return false;
        }
        return tryBody(buf.substr(0, split), buf.substr(split), symbolLed);
    };
    auto tryCorrectedBody = [&tryBody](const std::string &body) {
        return tryBody({}, body, canPeelSymbolLedBody(body));
    };

    // A repeated key can make an otherwise valid syllable fail slot validation
    // (`hkk` or `suu`, for example). If removing one occurrence from the short
    // failed hypothesis makes the whole tail convert, treat that occurrence as
    // the accidental duplicate. This is deferred until conversion succeeds, so
    // an ordinary repeated English letter remains untouched.
    if (duplicateKey_ != 0 && invalidSequenceLength_ > 0 &&
        invalidSequenceLength_ <= buf.size()) {
        for (std::size_t i = 0; i < invalidSequenceLength_; ++i) {
            if (!sameAsciiKey(buf[i], duplicateKey_)) {
                continue;
            }
            std::string corrected = buf;
            corrected.erase(i, 1);
            if (tryCorrectedBody(corrected)) {
                return true;
            }
        }
    }

    // A failed 注音 hypothesis is normally shown as literal English so users
    // can recover from it. If the exact suffix after that failed prefix later
    // proves to be a complete Chinese syllable, treat the prefix as an
    // accidental key and remove it. This is deliberately deferred until the
    // suffix converts, so ordinary English prefixes keep the existing peel
    // behavior.
    if (invalidPrefixLength_ > 0 && invalidPrefixLength_ < buf.size()) {
        if (tryCorrectedBody(buf.substr(invalidPrefixLength_))) {
            return true;
        }
    }

    // A punctuation-looking key may have been kept literal at a boundary because
    // it was ambiguous on its own. If the trailing literal tail later forms a
    // clear symbol-led zhuyin body, recover that longer suffix first instead of
    // peeling only the shortest alphabetic tail.
    for (std::size_t k = 0; k < buf.size(); ++k) {
        if (tryCandidate(k, /*symbolLed=*/true)) {
            return true;
        }
    }

    // Prefer the shortest trailing syllable (largest k) that actually forms a
    // character: steal as few letters as possible from the English word. This
    // keeps brand names like "acer" intact ("aceru/6" -> acer + 螢) without
    // relying on a dictionary, which would miss non-words.
    for (std::size_t k = buf.size(); k-- > 0;) {
        if (tryCandidate(k, /*symbolLed=*/false)) {
            return true;
        }
    }
    return false;
}

bool Buffer::tryPeelEnglishTone1(KeyResult &out) {
    const std::string &buf = englishBuf_;
    auto commit = [this, &out](const std::string &prefix,
                               const std::string &body,
                               const std::string &syllable) {
        freezeRun();
        for (char c : prefix) {
            cells_.push_back({false, std::string(1, c), {}});
        }
        zhuyin_.feedSequence(syllable);
        zhuyin_.handleSpace();
        runReadings_ = {syllable + " "};
        runTyped_ = {body + " "};
        zhuyin_.promoteUserPhrases();
        moveAutoCommit();
        token_ = Token::Chinese;
        englishBuf_.clear();
        invalidPrefixLength_ = 0;
        invalidSequenceLength_ = 0;
        duplicateKey_ = 0;
        syl_.clear();
        out = {true, false, {}, true};
        return true;
    };
    auto tryBody = [&commit](const std::string &prefix, const std::string &body,
                             bool symbolLed) {
        if (!ari_ime::isValidSyllable(body, /*allowTone=*/false)) {
            return false;
        }
        if (symbolLed) {
            if (!canPeelSymbolLedFromEnglish(prefix, body)) {
                return false;
            }
        } else if (!canPeelEnglishBody(prefix, body)) {
            return false;
        }
        const std::string syllable = ari_ime::canonicalKeys(body);
        if (!syllableConvertsTone1(syllable)) {
            return false;
        }
        return commit(prefix, body, syllable);
    };
    auto tryCandidate = [&buf, &tryBody](std::size_t split, bool symbolLed) {
        if (split >= buf.size()) {
            return false;
        }
        return tryBody(buf.substr(0, split), buf.substr(split), symbolLed);
    };
    auto tryCorrectedBody = [&tryBody](const std::string &body) {
        return tryBody({}, body, canPeelSymbolLedBody(body));
    };

    if (invalidPrefixLength_ > 0 && invalidPrefixLength_ < buf.size()) {
        if (tryCorrectedBody(buf.substr(invalidPrefixLength_))) {
            return true;
        }
    }
    if (duplicateKey_ != 0 && invalidSequenceLength_ > 0 &&
        invalidSequenceLength_ <= buf.size()) {
        for (std::size_t i = 0; i < invalidSequenceLength_; ++i) {
            if (!sameAsciiKey(buf[i], duplicateKey_)) {
                continue;
            }
            std::string corrected = buf;
            corrected.erase(i, 1);
            if (tryCorrectedBody(corrected)) {
                return true;
            }
        }
    }

    for (std::size_t k = 0; k < buf.size(); ++k) {
        if (tryCandidate(k, /*symbolLed=*/true)) {
            return true;
        }
    }
    return false;
}

KeyResult Buffer::handleSpace() {
    clearSelectionUndo();
    // A pending bopomofo syllable: space is its 一聲. Convert it if that yields a
    // character (a lone 聲母 like "t" does not — fall through to a literal space).
    if (!forcedEnglish_ && token_ == Token::Chinese && !syl_.empty()) {
        const std::string body = ari_ime::canonicalKeys(syl_);
        if (ari_ime::isValidSyllable(body, /*allowTone=*/false) &&
            syllableConvertsTone1(body)) {
            if (!englishBuf_.empty()) {
                freezeRun();
                freezeEnglish();
                zhuyin_.feedSequence(body);
                runReadings_.clear();
                runTyped_.clear();
            } else if (zhuyin_.hasConverted()) {
                for (char k : body) {
                    zhuyin_.handleDefault(static_cast<int>(k));
                }
            } else {
                zhuyin_.feedSequence(body);
                runReadings_.clear();
                runTyped_.clear();
            }
            zhuyin_.handleSpace();             // 一聲
            runReadings_.push_back(body + " "); // ' ' marks a 一聲 reading
            runTyped_.push_back(syl_ + " ");    // typed order, same sentinel
            zhuyin_.promoteUserPhrases();
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
    // Traditional注音/Rime users often expect Space to open candidates after a
    // complete conversion. Keep that behavior opt-in so Ari's default mixed
    // contract still treats Space as 一聲 or a literal separator.
    if (spaceCandidateMode_ && !forcedEnglish_ &&
        token_ == Token::Chinese && syl_.empty() && englishBuf_.empty() &&
        zhuyin_.hasConverted()) {
        return enterSelection(fcitx::Key(FcitxKey_Down),
                              /*startAtBeginning=*/true);
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
    if (learningAllowed_) {
        learnFromCells(); // teach chewing the chosen readings -> characters
    }
    reset();
    return {true, true, out, true};
}

void Buffer::learnFromCells() {
    struct ExplicitSpan {
        int start;
        int end;
        int runStart;
        int runEnd;
    };
    std::vector<ExplicitSpan> explicitSpans;

    int i = 0;
    while (i < static_cast<int>(cells_.size())) {
        if (!cells_[i].chinese) {
            ++i;
            continue;
        }
        for (int j = i; j < static_cast<int>(cells_.size()) &&
                        cells_[j].chinese;
             ++j) {
            if (!cells_[j].reading.empty()) {
                auto [entry, inserted] =
                    knownReadings_.try_emplace(cells_[j].text,
                                               cells_[j].reading);
                if (inserted) {
                    knownReadingsOrder_.push_back(entry->first);
                }
            }
        }
        constexpr std::size_t kMaxKnownReadings = 4096;
        while (knownReadings_.size() > kMaxKnownReadings &&
               !knownReadingsOrder_.empty()) {
            knownReadings_.erase(knownReadingsOrder_.front());
            knownReadingsOrder_.pop_front();
        }
        int s = i;
        while (i < static_cast<int>(cells_.size()) && cells_[i].chinese) {
            ++i;
        }
        int e = i - 1;

        // Unchanged output is still useful evidence: one accepted pass gives it
        // a low positive weight instead of treating "not selected" as wrong.
        learnRange(s, e, 1);

        for (int j = s; j <= e;) {
            if (cells_[j].selectionGroup == 0) {
                ++j;
                continue;
            }
            const int group = cells_[j].selectionGroup;
            const int groupStart = j;
            while (j <= e && cells_[j].selectionGroup == group) {
                ++j;
            }
            explicitSpans.push_back({groupStart, j - 1, s, e});
        }
    }

    for (const auto &span : explicitSpans) {
        // The weak pass above plus these three passes gives a deliberate choice
        // roughly four times the evidence of an unchanged conversion.
        if (!learnRange(span.start, span.end, 3)) {
            continue;
        }
        std::string phrase;
        for (int j = span.start; j <= span.end; ++j) {
            phrase += cells_[j].text;
        }
        // Only persist a deliberate choice after its exact text was replayed.
        zhuyin_.rememberPreferredPhrase(phrase);

        // One short context pass teaches where this choice was made without
        // turning an entire sentence into a high-weight personal phrase.
        const int contextStart = std::max(span.runStart, span.start - 1);
        const int contextEnd = std::min(span.runEnd, span.end + 1);
        if (contextStart != span.start || contextEnd != span.end) {
            learnRange(contextStart, contextEnd, 1);
        }
    }
    zhuyin_.resetAll();
}

bool Buffer::learnRange(int start, int end, int passes) {
    bool allReplayed = true;
    for (int chunkStart = start; chunkStart <= end;
         chunkStart += ari_ime::kMaxCompositionChars) {
        const int chunkEnd =
            std::min(end, chunkStart + ari_ime::kMaxCompositionChars - 1);
        std::string expected;
        for (int j = chunkStart; j <= chunkEnd; ++j) {
            expected += cells_[j].text;
        }
        for (int pass = 0; pass < passes; ++pass) {
            feedRun(chunkStart, chunkEnd, 0);
            relockRun(chunkStart, chunkEnd, /*onlyLocked=*/false);
            if (zhuyin_.preedit() != expected) {
                // A reading may no longer resolve to the displayed choice.
                // Never train a different phrase under that reading.
                zhuyin_.resetAll();
                allReplayed = false;
                break;
            }
            // Enter commits and updates libchewing's local model.
            zhuyin_.handleEnter();
            zhuyin_.takeCommit(); // Output already comes from cells_; discard replay.
        }
    }
    return allReplayed;
}

KeyResult Buffer::handleBackspace() {
    clearSelectionUndo();
    // Peel back through the live tail, then the run, then the finalized cells.
    if (!syl_.empty()) {
        syl_.pop_back();
        return {true, false, {}, true};
    }
    if (!englishBuf_.empty()) {
        englishBuf_.pop_back();
        // Once the user backspaces through the breaking key and reaches the
        // tagged failed hypothesis, that hypothesis is no longer accidental
        // input we can safely discard on a later suffix.
        if (invalidSequenceLength_ >= englishBuf_.size()) {
            invalidPrefixLength_ = 0;
            invalidSequenceLength_ = 0;
            duplicateKey_ = 0;
        }
        if (englishBuf_.empty() && !forcedEnglish_) {
            token_ = Token::Chinese;
        }
        return {true, false, {}, true};
    }
    if (!zhuyin_.preedit().empty()) {
        // libchewing keeps a syllable in canonical slot order. For a syllable
        // whose typed key order differs from that canonical order (for example
        // "140" → "104"), rebuild the preceding run and restore the user's
        // original keys. Canonical input keeps libchewing's native Backspace
        // path, which also preserves its normal phrase-editing semantics.
        if (!runReadings_.empty()) {
            const std::string reading = runReadings_.back();
            const std::string typed = runTyped_.empty()
                                          ? reading
                                          : runTyped_.back();
            const auto typedParts = typedBody(typed, reading);
            const auto readingParts = readingBody(reading);
            if (typedParts == readingParts) {
                zhuyin_.handleBackspace();
                runReadings_.pop_back();
                if (!runTyped_.empty()) {
                    runTyped_.pop_back();
                }
                return {true, false, {}, true};
            }

            runReadings_.pop_back();
            if (!runTyped_.empty()) {
                runTyped_.pop_back();
            }

            zhuyin_.resetAll();
            for (const std::string &savedReading : runReadings_) {
                auto [body, tone1] = readingBody(savedReading);
                for (char c : body) {
                    zhuyin_.handleDefault(static_cast<int>(c));
                }
                if (tone1) {
                    zhuyin_.handleSpace();
                }
            }
            zhuyin_.promoteUserPhrases();
            moveAutoCommit();
            syl_ = typedParts.first;
            return {true, false, {}, true};
        }

        // A run without Ari reading metadata can only arise from a defensive
        // fallback. Preserve the engine's usual behavior in that case.
        zhuyin_.handleBackspace();
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
    clearSelectionUndo();
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
    // Preserve libchewing's contextual conversion, then apply the explicit
    // preference sidecar only on older libchewing builds before restoring
    // explicitly locked picks that a fresh feed reverted.
    zhuyin_.promoteUserPhrases();
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
        for (int d = 0; d < ari_ime::kMaxSyllables; ++d) { // collapse to single chars
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
    // chewing_cand_choose_by_index takes a global index into nTotalChoice.
    // Do not page + pass idx%perPage — that is DoSelect's number-key path and
    // remaps high indices onto page-1 slots.
    zhuyin_.chooseCandidate(globalIdx);
}

void Buffer::buildSelCands() {
    selCands_.clear();
    selPage_ = 0;
    highlight_ = 0;
    const int targetOffset = selCursor_ - selRunStart_;

    auto parkAt = [this](int offset) {
        zhuyin_.closeCandidates();
        zhuyin_.handleHome();
        for (int i = 0; i < offset; ++i) {
            zhuyin_.handleRight();
        }
    };

    auto alreadyAdded = [this](const std::string &text, int startOffset) {
        return std::any_of(selCands_.begin(), selCands_.end(),
                           [&text, startOffset](const SelCand &candidate) {
                               return candidate.down >= 0 &&
                                      candidate.startOffset == startOffset &&
                                      candidate.text == text;
                           });
    };

    // Query only phrase intervals that start at the selected character and can
    // extend toward following characters. Do not inspect preceding cells: when
    // the target is B in A+B+C, candidates may cover B+C but never A+B.
    const int startOffset = targetOffset;
    parkAt(startOffset);
    if (zhuyin_.openCandidates()) {
        for (int down = 0, guard = 0;
             guard < ari_ime::kMaxSyllables; ++guard, ++down) {
            int total = zhuyin_.candidateCount();
            if (total <= 0) {
                break;
            }
            int charLen = utf8Count(zhuyin_.candidate(0));
            if (startOffset + charLen > targetOffset) {
                for (int i = 0; i < total; ++i) {
                    std::string text = zhuyin_.candidate(i);
                    if (startOffset + utf8Count(text) <= targetOffset ||
                        alreadyAdded(text, startOffset)) {
                        continue;
                    }
                    selCands_.push_back(
                        {text, text, down, i, startOffset,
                         static_cast<int>(selCands_.size())});
                }
            }
            if (charLen <= 1) {
                break; // reached the single-character interval
            }
            zhuyin_.handleDown(); // phrase -> shorter interval -> single
        }
        zhuyin_.closeCandidates();
    }
    parkAt(targetOffset);

    // Last entry: revert this character to its raw 注音 keys (English). down = -1
    // marks it; picking it explodes the cell instead of choosing a homophone.
    // Prefer the keys as actually typed so an out-of-order syllable (e.g. "ox"
    // for ㄜ) shows itself instead of the canonicalized form ("eo").
    std::string raw = typedBody(cells_[selCursor_].typed,
                                cells_[selCursor_].reading).first;
    if (!raw.empty()) {
        selCands_.push_back(
            {raw, "原始鍵 " + raw, -1, -1, targetOffset,
             static_cast<int>(selCands_.size())});
    }
    rankSelCands();
    appendSymbolKeyPunctuationCandidates();
}

void Buffer::appendSymbolKeyPunctuationCandidates() {
    if (selCursor_ < 0 || selCursor_ >= static_cast<int>(cells_.size()) ||
        !cells_[selCursor_].chinese) {
        return;
    }

    const std::string reading = readingBody(cells_[selCursor_].reading).first;
    if (reading.empty()) {
        return;
    }

    std::vector<std::string> punctuation;
    for (char key : reading) {
        if (!ari_ime::isSymbolLikeZhuyinKey(key)) {
            continue;
        }
        PhysicalPunctuationKey physicalKey{};
        if (!physicalPunctuationKeyForChar(key, physicalKey)) {
            continue;
        }
        for (std::string candidate : punctuationCandidatesForPhysicalKey(
                 physicalKey, fullWidthPunct_)) {
            appendUniquePunctuation(punctuation, std::move(candidate));
        }
    }
    if (punctuation.empty()) {
        return;
    }

    const int targetOffset = selCursor_ - selRunStart_;
    std::vector<SelCand> additions;
    int order = static_cast<int>(selCands_.size());
    for (const std::string &candidate : punctuation) {
        const bool alreadyPresent = std::any_of(
            selCands_.begin(), selCands_.end(), [&candidate](const SelCand &existing) {
                return existing.text == candidate;
            });
        if (alreadyPresent) {
            continue;
        }
        additions.push_back({candidate, candidate, kPunctuationCandidateDown, 0,
                             targetOffset, order++});
    }
    if (additions.empty()) {
        return;
    }

    // Preserve the complete native candidate ordering first. Punctuation
    // alternatives belong after every Chinese candidate but before the final
    // raw-key recovery entry.
    int insertAt = static_cast<int>(selCands_.size());
    for (int i = 0; i < static_cast<int>(selCands_.size()); ++i) {
        if (selCands_[i].down == -1) {
            insertAt = i;
            break;
        }
    }
    selCands_.insert(selCands_.begin() + insertAt, additions.begin(),
                     additions.end());
}

void Buffer::buildPunctuationCandidates() {
    selCands_.clear();
    selPage_ = 0;
    highlight_ = 0;

    const std::string current = cells_[selCursor_].text;
    int order = 0;
    selCands_.push_back(
        {current, current, kPunctuationCandidateDown, 0, 0, order++});

    auto add = [this, &current, &order](std::string candidate) {
        if (candidate == current ||
            std::any_of(selCands_.begin(), selCands_.end(),
                        [&candidate](const SelCand &existing) {
                            return existing.text == candidate;
                        })) {
            return;
        }
        selCands_.push_back(
            {candidate, candidate, kPunctuationCandidateDown, 0, 0, order++});
    };
    PhysicalPunctuationKey physicalKey{};
    if (physicalPunctuationKeyForText(current, physicalKey)) {
        for (const std::string &candidate :
             punctuationCandidatesForPhysicalKey(physicalKey, fullWidthPunct_)) {
            add(candidate);
        }
    }
}

int Buffer::visibleCandidateCount() const {
    if (!selecting_ || !candOpen_ || selCands_.empty()) {
        return 0;
    }
    int start = selPage_ * ari_ime::kCandPerPage;
    if (start < 0 || start >= static_cast<int>(selCands_.size())) {
        return 0;
    }
    return std::min(ari_ime::kCandPerPage,
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
        if (isPunctuationText(cells_[selCursor_].text)) {
            buildPunctuationCandidates();
        }
        // Ordinary English cell: no candidates. Punctuation cells were loaded
        // above from Ari's standalone symbol catalog.
        return;
    }
    int s, e;
    chineseRunAround(selCursor_, s, e);
    if (e - s + 1 > ari_ime::kMaxCompositionChars) {
        // Keep the target inside libchewing's active window. Prefer context on
        // both sides, then slide at the sentence edges.
        const int runStart = s;
        const int half = ari_ime::kMaxCompositionChars / 2;
        s = std::max(s, selCursor_ - half);
        e = std::min(e, s + ari_ime::kMaxCompositionChars - 1);
        s = std::max(runStart, e - ari_ime::kMaxCompositionChars + 1);
    }
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

KeyResult Buffer::enterSelection(const fcitx::Key &key, bool startAtBeginning) {
    mergeTail(); // reunite any mid-string insertion before re-opening editing
    if (cells_.empty()) {
        // Nothing pending: let the arrow key reach the application.
        return {false, false, {}, false};
    }
    // Enter caret mode at the requested edge, then let the triggering key
    // (←/→/↓) act. Normal navigation starts at the end; whole-phrase entry
    // points at the first cell so forward-only lookup can see the phrase.
    selecting_ = true;
    candOpen_ = false;
    caretPos_ = startAtBeginning ? 0 : static_cast<int>(cells_.size());
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

std::vector<int> Buffer::phraseBoundaries() {
    const int count = static_cast<int>(cells_.size());
    std::vector<int> boundaries{0, count};
    auto add = [&boundaries](int value) { boundaries.push_back(value); };

    for (int i = 0; i < count;) {
        if (cells_[i].chinese) {
            const int start = i;
            while (i < count && cells_[i].chinese) {
                ++i;
            }
            const int end = i;
            std::vector<bool> covered(end - start, false);
            for (int chunkStart = start; chunkStart < end;
                 chunkStart += ari_ime::kMaxCompositionChars) {
                const int chunkEnd = std::min(
                    end, chunkStart + ari_ime::kMaxCompositionChars);
                feedRun(chunkStart, chunkEnd - 1, 0);
                for (const auto &[from, to] : zhuyin_.phraseIntervals()) {
                    if (from < 0 || to <= from || chunkStart + to > chunkEnd) {
                        continue;
                    }
                    add(chunkStart + from);
                    add(chunkStart + to);
                    for (int j = chunkStart - start + from;
                         j < chunkStart - start + to; ++j) {
                        covered[j] = true;
                    }
                }
            }
            for (int j = 0; j < end - start; ++j) {
                if (!covered[j]) {
                    add(start + j);
                    add(start + j + 1);
                }
            }
            continue;
        }

        const bool asciiWord = cells_[i].text.size() == 1 &&
                               (std::isalnum(static_cast<unsigned char>(
                                    cells_[i].text[0])) ||
                                cells_[i].text[0] == '_');
        const int start = i++;
        if (asciiWord) {
            while (i < count && !cells_[i].chinese &&
                   cells_[i].text.size() == 1 &&
                   (std::isalnum(static_cast<unsigned char>(cells_[i].text[0])) ||
                    cells_[i].text[0] == '_')) {
                ++i;
            }
        }
        add(start);
        add(i);
    }

    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    // A phrase explicitly chosen by the user is always one navigation unit,
    // even if a later model refresh would segment it differently.
    boundaries.erase(
        std::remove_if(boundaries.begin(), boundaries.end(), [this, count](int b) {
            return b > 0 && b < count && cells_[b - 1].selectionGroup != 0 &&
                   cells_[b - 1].selectionGroup == cells_[b].selectionGroup;
        }),
        boundaries.end());
    zhuyin_.resetAll();
    runLoaded_ = false;
    return boundaries;
}

KeyResult Buffer::moveCaretByPhrase(int direction) {
    const auto boundaries = phraseBoundaries();
    if (direction < 0) {
        const auto it = std::lower_bound(boundaries.begin(), boundaries.end(),
                                         caretPos_);
        if (it != boundaries.begin()) {
            caretPos_ = *std::prev(it);
        }
    } else {
        const auto it = std::upper_bound(boundaries.begin(), boundaries.end(),
                                         caretPos_);
        if (it != boundaries.end()) {
            caretPos_ = *it;
        }
    }
    return {true, false, {}, true};
}

KeyResult Buffer::pickCandidate(int pageIndex) {
    int gi = selPage_ * ari_ime::kCandPerPage + pageIndex;
    if (gi < 0 || gi >= static_cast<int>(selCands_.size())) {
        candOpen_ = false; // no such candidate: fall back to caret mode
        caretPos_ = selCursor_;
        return {true, false, {}, true};
    }
    SelCand sc = selCands_[gi];
    rememberSelectionUndo();
    if (sc.down == kPunctuationCandidateDown) {
        // Standalone punctuation candidates target the literal cell itself.
        // Candidates merged into a Chinese run carry the selected cell's offset
        // so choosing one replaces only that Chinese character and leaves the
        // surrounding run intact.
        int target = selCursor_;
        if (cells_[selCursor_].chinese) {
            target = selRunStart_ + sc.startOffset;
        }
        if (target >= 0 && target < static_cast<int>(cells_.size())) {
            cells_[target] = {false, sc.text, {}};
        }
        finishPickAt(target + 1);
        return {true, false, {}, true};
    }
    if (sc.down < 0) {
        return revertCellToEnglish(); // the "raw keys" entry
    }
    // Choose on the LIVE run (no re-feed) so chewing keeps earlier picks locked.
    // Reposition the cursor, walk to the candidate's interval, and choose it. A
    // phrase pick rewrites several cells, a single-character pick just one;
    // applyRun copies chewing's buffer back over the run's cells either way.
    int offset = sc.startOffset;
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
    const int selectionGroup = nextSelectionGroup_++;
    const int pickedStart = selRunStart_ + sc.startOffset;
    for (int j = pickedStart; j < pickedStart + picked &&
                            j <= selRunEnd_ && j < static_cast<int>(cells_.size());
         ++j) {
        cells_[j].locked = true;
        cells_[j].selectionGroup = selectionGroup;
    }
    // A completed pick closes the candidate window but keeps caret mode, with the
    // caret on the character right after the text that was just rewritten. The
    // user corrected something mid-sentence, so that is where editing continues;
    // End (or Escape) still returns to appending at the tail.
    finishPickAt(pickedStart + picked);
    return {true, false, {}, true};
}

void Buffer::finishPickAt(int caret) {
    candOpen_ = false;
    selCands_.clear();
    selPage_ = 0;
    highlight_ = 0;
    if (cells_.empty() ||
        caretAfterPick_ == ari_ime::CaretAfterPick::EndOfText) {
        // Leave editing entirely: the caret reports "at the very end" and the
        // next printable key resumes the normal append path.
        exitSelection();
        return;
    }
    const int n = static_cast<int>(cells_.size());
    caretPos_ = std::clamp(caret, 0, n);
    // The caret sits BETWEEN cells; selCursor_ names a cell, so park it on the
    // one the caret points at (the last cell when the caret is at the very end).
    selCursor_ = std::min(caretPos_, n - 1);
    // Leave chewing's own candidate window closed. The run stays loaded, but
    // every path that reopens candidates (openCandidatesAt / moveSelCursor)
    // clears runLoaded_ first, so a later pick always re-feeds from the cells.
    zhuyin_.closeCandidates();
}

KeyResult Buffer::changeCandidatePage(int delta, bool wrap) {
    const int total = static_cast<int>(selCands_.size());
    const int totalPages =
        (total + ari_ime::kCandPerPage - 1) / ari_ime::kCandPerPage;
    if (totalPages <= 0) {
        selPage_ = 0;
        highlight_ = 0;
        return {true, false, {}, true};
    }
    int page = selPage_ + delta;
    if (wrap) {
        // Cycle like libchewing's own candidate window (and like ↓/↑ here):
        // past the last page is the first, before the first is the last.
        page = ((page % totalPages) + totalPages) % totalPages;
    } else {
        page = std::clamp(page, 0, totalPages - 1);
    }
    selPage_ = page;
    highlight_ = 0;
    return {true, false, {}, true};
}

void Buffer::rememberSelectionUndo() {
    constexpr std::size_t kMaxSelectionUndo = 8;
    if (selectionUndo_.size() == kMaxSelectionUndo) {
        selectionUndo_.erase(selectionUndo_.begin());
    }
    selectionUndo_.push_back({cells_, nextSelectionGroup_});
}

void Buffer::clearSelectionUndo() { selectionUndo_.clear(); }

KeyResult Buffer::undoSelection() {
    SelectionUndo snapshot = std::move(selectionUndo_.back());
    selectionUndo_.pop_back();
    exitSelection();
    cells_ = std::move(snapshot.cells);
    runReadings_.clear();
    runTyped_.clear();
    tail_.clear();
    englishBuf_.clear();
    syl_.clear();
    token_ = Token::Chinese;
    return {true, false, {}, true, false, "已復原選字"};
}

KeyResult Buffer::forgetHighlightedCandidate() {
    const int gi = selPage_ * ari_ime::kCandPerPage + highlight_;
    if (gi < 0 || gi >= static_cast<int>(selCands_.size()) ||
        selCands_[gi].down < 0) {
        return {true, false, {}, false, false, "這個項目沒有個人學習紀錄"};
    }

    const std::string phrase = selCands_[gi].text;
    const int removed = zhuyin_.forgetUserPhrase(phrase);
    if (removed <= 0) {
        return {true, false, {}, false, false,
                removed < 0 ? "無法移除個人學習紀錄"
                            : "這個候選沒有個人學習紀錄"};
    }

    runLoaded_ = false;
    loadCellCandidates();
    return {true, false, {}, true, false, "已忘記「" + phrase + "」"};
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
    clearSelectionUndo();
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
    for (const std::string &ch : ari_ime::unicode::splitGraphemes(text)) {
        if (isIgnoredPasteFormat(ch)) {
            continue;
        }
        // Clipboard data is untrusted: drop clusters that are not well-formed
        // UTF-8 instead of committing broken bytes to the client, and fold C1
        // controls (validly encoded but invisible) in with the separators.
        bool clusterValid = !ch.empty();
        for (std::size_t off = 0; off < ch.size();) {
            const ari_ime::unicode::CodePoint cp =
                ari_ime::unicode::decode(ch, off);
            if (!cp.valid) {
                clusterValid = false;
                break;
            }
            if (cp.value >= 0x80 && cp.value <= 0x9F) {
                clusterValid = false;
                break;
            }
            off += cp.length;
        }
        if (!clusterValid) {
            if (!pasted.empty() && pasted.back().text != " ") {
                pasted.push_back({false, " ", {}});
            }
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
    auto [typed, tone1] =
        typedBody(cells_[selCursor_].typed, cells_[selCursor_].reading);
    (void)tone1; // 一聲 has no key of its own; the body alone explodes
    if (typed.empty()) {
        return {true, false, {}, true};
    }
    cells_.erase(cells_.begin() + selCursor_);
    int at = selCursor_;
    for (char c : typed) {
        cells_.insert(cells_.begin() + at,
                      {false, std::string(1, c), {}});
        ++at;
    }
    runLoaded_ = false;   // cell layout changed
    candOpen_ = false;    // back to caret mode, caret right after the exploded keys
    // The raw-keys entry was consumed; drop its candidate list so a later
    // reinterpret cannot reopen a window wired to the old selection run.
    selCands_.clear();
    selPage_ = 0;
    highlight_ = 0;
    caretPos_ = at;
    selCursor_ = at - 1;
    return {true, false, {}, true};
}

KeyResult Buffer::showBopomofoForCell(int cell) {
    const std::string key = cells_[cell].text;
    // One key only, and only one that means something on this layout. Anything
    // else (a letter with no 注音 slot, a multi-codepoint literal) is left alone
    // so ↑ stays a no-op rather than mangling text.
    if (key.size() != 1 ||
        ari_ime::zhuyinSlot(key[0]) == ari_ime::kNoZhuyinSlot) {
        return {true, false, {}, true};
    }
    const std::string symbol = bopomofoForKeys(key);
    if (symbol.empty()) {
        return {true, false, {}, true};
    }
    clearSelectionUndo(); // a structural edit, like reinterpretFromCell
    cells_[cell] = {false, symbol, {}};
    // Stay in caret mode with the caret just after the symbol, so the next
    // keystroke continues there like every other in-place edit.
    candOpen_ = false;
    selCands_.clear();
    selPage_ = 0;
    highlight_ = 0;
    runLoaded_ = false; // the cell's content changed under any loaded run
    caretPos_ = cell + 1;
    selCursor_ = cell;
    return {true, false, {}, true};
}

KeyResult Buffer::reinterpretFromCell() {
    clearSelectionUndo();
    // Accumulate raw keys from the cursor cell forward (English cells contribute
    // their letter; a Chinese cell contributes its reading) until they form a
    // complete, convertible syllable. Look only a few cells ahead.
    const std::string currentKeys = cells_[selCursor_].chinese
                                        ? readingBody(cells_[selCursor_].reading).first
                                        : cells_[selCursor_].text;
    const bool symbolLed =
        !currentKeys.empty() && ari_ime::isSymbolLikeZhuyinKey(currentKeys.front());
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
            return ari_ime::isSymbolLikeZhuyinKey(c);
        });
    };
    for (int j = selCursor_; j < limit; ++j) {
        std::string keys = cells_[j].chinese
                               ? readingBody(cells_[j].reading).first
                               : cells_[j].text;
        std::string trial = ari_ime::canonicalKeys(raw + keys);
        if (!ari_ime::isValidSyllable(trial, /*allowTone=*/true)) {
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
    cells_.insert(cells_.begin() + selCursor_, {true, {}, found, raw});
    loadCellCandidates();
    // Fill the new cell's text from chewing's converted buffer (one character at
    // the cursor offset), not the candidate list (whose top item may be a phrase).
    auto chars = splitUtf8(zhuyin_.preedit());
    int k = selCursor_ - selRunStart_;
    if (k < 0 || k >= static_cast<int>(chars.size())) {
        // Chewing's fed preedit came up short for this offset. Bail out with a
        // single-codepoint placeholder instead of storing the multi-byte raw
        // syllable in a Chinese cell: applyRunToCells maps one codepoint per
        // cell, and an oversized text here would leave stale glyphs behind.
        cells_[selCursor_] = {true, "？", found, raw};
        return {true, false, {}, true};
    }
    cells_[selCursor_].text = chars[k];
    return {true, false, {}, true};
}

KeyResult Buffer::handleSelecting(const fcitx::Key &key) {
    // Two sub-modes: a bare caret between characters (insert / navigate) and the
    // candidate window over one cell (re-pick). Number keys only ever pick in the
    // latter, so 注音 starting with a number-row key inserts cleanly in the former.
    //
    // Modifier-only keydowns (Shift_L/R, Ctrl, Alt, Super, CapsLock) must not
    // leave picking or caret mode. Wayland compositors such as Hyprland deliver
    // Shift as its own event before Shift+Delete; treating that as a generic
    // control key would close the candidate window and make forget-candidate
    // unreachable. Leave the event unhandled so the compositor still tracks
    // modifier state for the following key.
    if (isModifierOnlyKeySym(key.sym())) {
        return {false, false, {}, false};
    }
    return candOpen_ ? handlePicking(key) : handleCaret(key);
}

KeyResult Buffer::handleCaret(const fcitx::Key &key) {
    auto sym = normalizeKeySym(key.sym());
    const int N = static_cast<int>(cells_.size());

    const bool ctrlNavigation =
        key.states().test(fcitx::KeyState::Ctrl) &&
        !key.states().testAny(fcitx::KeyStates{
            fcitx::KeyState::Shift, fcitx::KeyState::Alt,
            fcitx::KeyState::Super});
    if (ctrlNavigation && sym == FcitxKey_Left) {
        return moveCaretByPhrase(-1);
    }
    if (ctrlNavigation && sym == FcitxKey_Right) {
        return moveCaretByPhrase(1);
    }

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
        if (forcedEnglish_) {
            return {true, false, {}, false};
        }
        int cell = caretPos_ < N ? caretPos_ : N - 1;
        return openCandidatesAt(cell, /*reinterpret=*/sym == FcitxKey_Up);
    }
    if (sym == FcitxKey_BackSpace) {
        clearSelectionUndo();
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
        clearSelectionUndo();
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
    const bool punctuation = isPunctuationText(cells_[cell].text);
    const bool symbolLikeZhuyin =
        punctuation && cells_[cell].text.size() == 1 &&
        ari_ime::isSymbolLikeZhuyinKey(cells_[cell].text.front());
    if (cells_[cell].chinese ||
        (punctuation && (!reinterpret || !symbolLikeZhuyin))) {
        candOpen_ = true;
        loadCellCandidates();
        return {true, false, {}, true};
    }
    // English cell: only ↑ acts — fold it (+ the next few) back into a 注音
    // character and open its candidates. ↓ on English has nothing to pick.
    if (reinterpret) {
        if (literalKeyReinterpret_ ==
            ari_ime::LiteralKeyReinterpret::BopomofoSymbol) {
            return showBopomofoForCell(cell);
        }
        // reinterpretFromCell() early-outs without touching selCands_ on
        // failure paths; a stale list from a previous picking session would
        // reopen a window wired to the wrong cell and let the next pick land
        // on stale run indices. Clear it so candOpen_ only reflects a fresh
        // rebuild.
        selCands_.clear();
        selPage_ = 0;
        highlight_ = 0;
        KeyResult r = reinterpretFromCell();
        candOpen_ = !selCands_.empty(); // false if nothing converted
        return r;
    }
    return {true, false, {}, true};
}

KeyResult Buffer::handlePicking(const fcitx::Key &key) {
    auto sym = normalizeKeySym(key.sym());

    if (sym == FcitxKey_Delete &&
        key.states().test(fcitx::KeyState::Shift)) {
        return forgetHighlightedCandidate();
    }

    const bool ctrlNavigation =
        key.states().test(fcitx::KeyState::Ctrl) &&
        !key.states().testAny(fcitx::KeyStates{
            fcitx::KeyState::Shift, fcitx::KeyState::Alt,
            fcitx::KeyState::Super});
    if (ctrlNavigation &&
        (sym == FcitxKey_Left || sym == FcitxKey_Right)) {
        candOpen_ = false;
        caretPos_ = selCursor_;
        return moveCaretByPhrase(sym == FcitxKey_Left ? -1 : 1);
    }

    // ←/→ step to the adjacent character's candidates (fix several in a row),
    // or page through this character's list when the user configured that. The
    // mode they did not pick stays reachable: PageUp/PageDown always page, and
    // Escape drops to caret mode where ←/→ always move.
    if ((sym == FcitxKey_Left || sym == FcitxKey_Right) &&
        candidateArrowKeys_ == ari_ime::CandidateArrowKeys::ChangePage) {
        return changeCandidatePage(sym == FcitxKey_Right ? 1 : -1,
                                   /*wrap=*/true);
    }
    if (sym == FcitxKey_Left) {
        return moveSelCursor(-1);
    }
    if (sym == FcitxKey_Right) {
        if (selCursor_ + 1 < static_cast<int>(cells_.size())) {
            return moveSelCursor(+1);
        }
        // The caret is allowed to sit just after the final cell. Leaving the
        // candidate window here makes Right a natural "append at end" action
        // instead of trapping the user on the last character.
        candOpen_ = false;
        caretPos_ = static_cast<int>(cells_.size());
        selCands_.clear();
        selPage_ = 0;
        highlight_ = 0;
        zhuyin_.closeCandidates();
        return {true, false, {}, true};
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
        clearSelectionUndo();
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
        (total + ari_ime::kCandPerPage - 1) / ari_ime::kCandPerPage;
    int pageCount = std::min(ari_ime::kCandPerPage,
                             total - selPage_ * ari_ime::kCandPerPage);

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
                std::min(ari_ime::kCandPerPage, total - selPage_ * ari_ime::kCandPerPage) - 1;
        } else {
            selPage_ = totalPages > 0 ? totalPages - 1 : 0;
            highlight_ =
                std::min(ari_ime::kCandPerPage, total - selPage_ * ari_ime::kCandPerPage) - 1;
        }
        if (highlight_ < 0) {
            highlight_ = 0;
        }
        return {true, false, {}, true};
    }
    if (sym == FcitxKey_Page_Down) {
        return changeCandidatePage(1, /*wrap=*/false);
    }
    if (sym == FcitxKey_Page_Up) {
        return changeCandidatePage(-1, /*wrap=*/false);
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
