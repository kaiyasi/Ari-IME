// Assertion-based regression tests for the Buffer state machine. Exits non-zero
// on any failure so it can gate changes.
//
// Build & run:
//   cmake --build build
//   ctest --test-dir build --output-on-failure

#include <string>
#include <vector>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include "buffer.h"
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
};

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
    // Live typing normalizes to the top selection candidate (你好), matching what
    // the candidate window offers first — see normalizeRunToTop.
    check_eq(pe("su3cl3"), "你好", "type su3cl3");
    check_eq(pe("ji3"), "我", "type ji3");
    check_eq(pe("hello"), "hello", "type hello (english)");
    check_eq(pe("apple"), "apple", "type apple (english)");
    check_eq(pe("su3hello"), "你hello", "type su3hello (mixed)");
    check_eq(pe("s3u"), "你", "type s3u (out of order)");
    check_eq(pe("su"), "su", "type su (no tone stays raw)");
    check_eq(pe("aceru/6aj4"), "acer螢幕", "type aceru/6aj4 (peel)");
    check_eq(pe("su3g4"), "你是", "type su3g4 (phrasing)");
    // Bopomofo has no case: uppercase 注音 keys (Shift / Caps Lock) convert the
    // same as lowercase. But uppercase that can't form Chinese stays English,
    // preserving the original case (brand names, acronyms).
    check_eq(pe("SU3"), "你", "type SU3 uppercase -> 你");
    check_eq(pe("Su3CL3"), "你好", "type mixed-case 注音 -> 你好");
    check_eq(pe("Acer"), "Acer", "uppercase-led English keeps its case");
    check_eq(pe("API"), "API", "all-caps acronym stays English");
}

void test_common_mixed_literals() {
    auto pe = [](const std::string &keys) {
        Sim s;
        s.type(keys);
        return s.preedit();
    };

    check_eq(pe("kai@example.com"), "kai@example.com",
             "email address stays literal");
    check_eq(pe("https://ari-ime.test/v0.2.3"),
             "https://ari-ime.test/v0.2.3",
             "URL with version path stays literal");
    check_eq(pe("Ari-IME-0.2.3"), "Ari-IME-0.2.3",
             "hyphenated version string stays literal");
    check_eq(pe("README.md"), "README.md", "filename stays literal");
    check_eq(pe("APIji3"), "API我", "acronym followed by zhuyin converts");
    check_eq(pe("HTTPsu3"), "HTTP你", "uppercase acronym followed by zhuyin converts");
}

void test_eten_typing() {
    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Eten);

    Sim s;
    s.b.setKeyboardLayout(inputer::KeyboardLayout::Eten);
    s.type("ne3"); // 倚天: ㄋㄧˇ
    check_eq(s.preedit(), "你", "Eten type ne3 -> 你");

    Sim o;
    o.b.setKeyboardLayout(inputer::KeyboardLayout::Eten);
    o.type("n3e");
    check_eq(o.preedit(), "你", "Eten accepts out-of-order n3e");

    Sim phrase;
    phrase.b.setKeyboardLayout(inputer::KeyboardLayout::Eten);
    phrase.type("ne3hz3");
    check_eq(phrase.preedit(), "你好", "Eten type ne3hz3 -> 你好");

    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);
}

void test_hsu_typing() {
    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Hsu);

    Sim s;
    s.b.setKeyboardLayout(inputer::KeyboardLayout::Hsu);
    s.type("nef"); // 許氏: ㄋㄧˇ
    check_eq(s.preedit(), "你", "Hsu type nef -> 你");

    Sim o;
    o.b.setKeyboardLayout(inputer::KeyboardLayout::Hsu);
    o.type("nfe");
    check_eq(o.preedit(), "你", "Hsu accepts out-of-order nfe");

    Sim phrase;
    phrase.b.setKeyboardLayout(inputer::KeyboardLayout::Hsu);
    phrase.type("nefhwf");
    check_eq(phrase.preedit(), "你好", "Hsu type nefhwf -> 你好");

    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);
}

void test_additional_layout_typing() {
    struct Case {
        inputer::KeyboardLayout layout;
        const char *name;
        const char *ni;
        const char *hao;
    };
    const Case cases[] = {
        {inputer::KeyboardLayout::Ibm, "IBM", "7a,", "-;,"},
        {inputer::KeyboardLayout::GinYieh, "精業", "d-a", "vla"},
        {inputer::KeyboardLayout::Dvorak, "Dvorak", "og3", "jn3"},
        {inputer::KeyboardLayout::Carpalx, "Carpalx", "su3", "cl3"},
        {inputer::KeyboardLayout::ColemakDhAnsi, "Colemak-DH ANSI", "rl3",
         "di3"},
        {inputer::KeyboardLayout::ColemakDhOrth, "Colemak-DH Ortholinear",
         "rl3", "ci3"},
        {inputer::KeyboardLayout::Workman, "Workman", "sf3", "mo3"},
        {inputer::KeyboardLayout::Colemak, "Colemak", "rl3", "ci3"},
    };

    for (const auto &c : cases) {
        inputer::setCurrentKeyboardLayout(c.layout);

        Sim single;
        single.b.setKeyboardLayout(c.layout);
        single.type(c.ni);
        std::string singleLabel = std::string(c.name) + " types 你";
        check_eq(single.preedit(), "你", singleLabel.c_str());

        Sim phrase;
        phrase.b.setKeyboardLayout(c.layout);
        phrase.type(std::string(c.ni) + c.hao);
        std::string phraseLabel = std::string(c.name) + " types 你好";
        check_eq(phrase.preedit(), "你好", phraseLabel.c_str());
    }

    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);
}

void test_layout_switch_resets_preedit() {
    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);

    Sim s;
    s.type("su3");
    check_eq(s.preedit(), "你", "layout switch setup has preedit");
    check(!s.b.setKeyboardLayout(inputer::KeyboardLayout::Default),
          "same layout switch reports unchanged");
    check_eq(s.preedit(), "你", "same layout keeps preedit");

    check(s.b.setKeyboardLayout(inputer::KeyboardLayout::Eten),
          "different layout switch reports changed");
    check_eq(s.preedit(), "", "layout switch clears preedit");
    s.type("ne3");
    check_eq(s.preedit(), "你", "typing resumes with switched layout");

    s.b.setKeyboardLayout(inputer::KeyboardLayout::Default);
    inputer::setCurrentKeyboardLayout(inputer::KeyboardLayout::Default);
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
    Sim s;
    s.type("su3cl3");       // 你好
    s.key(FcitxKey_KP_Home); // NumLock off keypad Home
    s.type("1j4");          // 不
    check_eq(s.preedit(), "不你好", "keypad Home moves caret to front");

    Sim begin;
    begin.type("su3cl3");       // 你好
    begin.key(FcitxKey_KP_Begin); // NumLock off keypad 5 / Begin
    begin.type("1j4");          // 不
    check_eq(begin.preedit(), "不你好",
             "keypad Begin moves caret to front");

    Sim e;
    e.type("su3cl3");       // 你好
    e.key(FcitxKey_KP_Home);
    e.key(FcitxKey_KP_End);
    e.type("1j4");          // 不
    check_eq(e.preedit(), "你好不", "keypad End moves caret to end");

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

void test_backspace() {
    Sim s;
    s.type("su3cl3"); // 你好
    s.key(FcitxKey_BackSpace);
    check_eq(s.preedit(), "你", "backspace deletes 好");
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
    check(c.size() > 2 && c[2] == "你", "single 你 after phrases");
}

// Live typing and the selection window must agree on the preferred phrase:
// normalizeRunToTop re-picks candidate[0] for multi-char intervals so the text
// shown while typing equals what selection offers first (你好, not auto 妳好).
void test_live_matches_top_candidate() {
    Sim s;
    s.type("su3cl3");
    check_eq(s.preedit(), "你好", "live shows top candidate 你好");
    s.key(FcitxKey_Left); // caret between 你 and 好
    s.key(FcitxKey_Left); // caret before 你
    s.key(FcitxKey_Down); // open candidates for 你
    auto c = s.cand();
    check(!c.empty() && c[0] == "你好", "selection top candidate matches live");
}

void test_phrase_pick() {
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left); // caret between 你 and 好
    s.key(FcitxKey_Left); // caret before 你
    s.key(FcitxKey_Down); // open candidates for 你
    s.key('2');           // pick #2 = 妳好 (phrase)
    check_eq(s.preedit(), "妳好", "phrase pick rewrites both cells");
    s.type("1j4");
    check_eq(s.preedit(), "妳好不",
             "typing after end-of-line phrase pick appends at tail");
}

void test_candidate_direct_selection() {
    Sim empty;
    KeyResult r = empty.b.selectCandidate(0);
    check(!r.handled, "direct candidate selection without window passes through");

    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left); // caret between 你 and 好
    s.key(FcitxKey_Left); // caret before 你
    s.key(FcitxKey_Down); // open candidates for 你
    r = s.b.selectCandidate(1);
    check(r.handled, "direct candidate selection handles visible candidate");
    check_eq(s.preedit(), "妳好",
             "direct candidate selection rewrites phrase like number key");

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

void test_pin_earlier_pick() {
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left);  // caret between 你 and 好
    s.key(FcitxKey_Left);  // caret before 你
    s.key(FcitxKey_Down);  // open candidates for 你 (hl0 你好)
    s.key(FcitxKey_Down);  // 妳好
    s.key(FcitxKey_Down);  // 你 (single)
    s.key(FcitxKey_Down);  // 妳 (single)
    s.key(FcitxKey_Return); // pick 妳; window advances to the next cell (好)
    check_eq(s.preedit(), "妳好", "picked 妳 single");
    check(s.b.selectionChar() == 1, "pick advances the window to the next cell");
    // Still picking, now on 好: fix it to 郝. The earlier 妳 pick must stay locked.
    s.key(FcitxKey_Down);  // 郝
    s.key(FcitxKey_Return);
    check_eq(s.preedit(), "妳郝", "earlier 妳 stays locked after picking 郝");
    s.type("1j4");
    check_eq(s.preedit(), "妳郝不",
             "typing after consecutive picks appends after fixed text");
}

// The headline fix: a 注音 syllable whose FIRST key is a number-row key (不 =
// ㄅㄨˋ = "1j4") must insert at the caret, not get eaten as a candidate pick.
void test_insert_chinese_midstring() {
    Sim s;
    s.type("su3cl3");       // 你好
    s.key(FcitxKey_Left);   // caret between 你 and 好
    s.key(FcitxKey_Left);   // caret before 你 (front)
    s.type("1j4");          // ㄅㄨˋ = 不, leading digit must NOT pick a candidate
    check_eq(s.preedit(), "不你好", "insert digit-led 注音 不 at the front");
    check(s.b.caretChar() == 1, "caret sits after the inserted 不, not at end");
    s.key(FcitxKey_Return);
    check_eq(s.committed, "不你好", "commit includes inserted char + tail");

    // Insert in the middle, not just the front.
    Sim m;
    m.type("su3cl3");       // 你好
    m.key(FcitxKey_Left);   // caret between 你 and 好
    m.type("1j4");          // insert 不 before 好
    check_eq(m.preedit(), "你不好", "insert 不 between 你 and 好");
    check(m.b.caretChar() == 2, "caret sits between 不 and 好, not at end");
}

// Paste lands at the caret like any other action, and typing continues there.
void test_paste_at_caret() {
    Sim fresh;
    fresh.b.pasteAtCaret("ABC");
    check_eq(fresh.preedit(), "ABC", "paste can start a fresh pre-edit");
    fresh.type("1j4");
    check_eq(fresh.preedit(), "ABC不",
             "typing continues after paste-started pre-edit");

    Sim s;
    s.type("su3cl3");      // 你好
    s.key(FcitxKey_Left);  // caret between 你 and 好
    s.b.pasteAtCaret("ABC");
    check_eq(s.preedit(), "你ABC好", "paste lands at the caret");
    check(s.b.caretChar() == 4, "caret sits right after the pasted text");
    s.type("1j4");         // keep composing at the caret -> insert 不 after ABC
    check_eq(s.preedit(), "你ABC不好", "typing continues at the caret after paste");

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
    Sim s;
    s.type("su3");        // 你
    s.key(FcitxKey_Down); // enter + open candidates for 你
    s.key(FcitxKey_Up);   // wrap to the last entry = raw-keys revert
    auto c = s.cand();
    check(!c.empty() && c.back() == "原始鍵 su3",
          "raw-keys candidate is labeled");
    s.key(FcitxKey_Return);
    check_eq(s.preedit(), "su3", "revert entry explodes 你 -> su3");
}

void test_candidate_paging() {
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
    s.type("catsu3");     // cats以 (auto peel)
    check_eq(s.preedit(), "cats以", "catsu3 auto-guesses cats以");
    s.key(FcitxKey_Left); // caret between s and 以
    s.key(FcitxKey_Left); // caret before s (so ↑ targets s)
    s.key(FcitxKey_Up);   // reinterpret the s (+ 以) -> 你
    check_eq(s.preedit(), "cat你", "reinterpret recovers cat你");

    Sim filename;
    filename.type("README.md");
    filename.key(FcitxKey_Home);
    filename.key(FcitxKey_Up);
    check_eq(filename.preedit(), "README.md",
             "reinterpret does not rewrite filename literal");
    check(filename.cand().empty(),
          "filename reinterpret no-op does not open candidates");

    Sim version;
    version.type("Ari-IME-0.2.3");
    version.key(FcitxKey_Home);
    version.key(FcitxKey_End);
    version.key(FcitxKey_Left); // caret before final 3
    version.key(FcitxKey_Left); // caret before '.'
    version.key(FcitxKey_Up);
    check_eq(version.preedit(), "Ari-IME-0.2.3",
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
    s.key('2');               // pick the 2nd candidate (a phrase 妳好)
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
    Sim s;
    s.type("su3cl3");       // 你好
    s.key(FcitxKey_Left);   // caret between 你 and 好
    s.key(FcitxKey_Delete); // delete the char right of the caret (好)
    check_eq(s.preedit(), "你", "caret delete removes char to the right");

    Sim h;
    h.type("su3cl3");       // 你好
    h.key(FcitxKey_Left);   // enter caret mode
    h.key(FcitxKey_Home);   // caret before 你
    h.type("1j4");          // 不
    check_eq(h.preedit(), "不你好", "Home moves insertion to the front");

    Sim e;
    e.type("su3cl3");       // 你好
    e.key(FcitxKey_Left);   // caret between 你 and 好
    e.key(FcitxKey_Home);   // front
    e.key(FcitxKey_End);    // end
    e.type("1j4");          // 不
    check_eq(e.preedit(), "你好不", "End moves insertion to the end");
}

void test_direct_navigation_enters_editing() {
    Sim empty;
    KeyResult r = empty.press(FcitxKey_Home);
    check(!r.handled, "Home with no pre-edit passes through");
    r = empty.press(FcitxKey_Up);
    check(!r.handled, "Up with no pre-edit passes through");

    Sim h;
    h.type("su3cl3");      // 你好
    h.key(FcitxKey_Home);  // direct caret mode at front
    h.type("1j4");         // 不
    check_eq(h.preedit(), "不你好", "top-level Home enters editing at front");

    Sim b;
    b.type("su3cl3");       // 你好
    b.key(FcitxKey_Begin);  // direct Begin at front
    b.type("1j4");          // 不
    check_eq(b.preedit(), "不你好", "top-level Begin enters editing at front");

    Sim e;
    e.type("su3cl3");      // 你好
    e.key(FcitxKey_Home);  // front
    e.key(FcitxKey_End);   // direct End to tail while editing
    e.type("1j4");         // 不
    check_eq(e.preedit(), "你好不", "top-level End moves editing caret to tail");

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
    check_eq(picking.preedit(), "不你",
             "typing after candidate Escape resumes at caret");

    Sim caret;
    caret.type("su3cl3");
    caret.key(FcitxKey_Home);
    caret.key(FcitxKey_Escape);
    check_eq(caret.preedit(), "你好", "Escape exits caret mode without clearing");
    caret.type("1j4");
    check_eq(caret.preedit(), "你好不",
             "typing after caret Escape resumes at end");
}

void test_picking_delete_focused_cell() {
    Sim s;
    s.type("su3cl3");          // 你好
    s.key(FcitxKey_Left);      // caret between 你 and 好
    s.key(FcitxKey_Left);      // caret before 你
    s.key(FcitxKey_Down);      // candidate window focused on 你
    s.key(FcitxKey_BackSpace); // delete focused 你, not char left of caret
    check_eq(s.preedit(), "好", "picking backspace deletes focused cell");
    check(s.b.selectionChar() == -1, "candidate window closes after focused delete");

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
    // Default (half-width): punctuation keys stay literal English.
    Sim h;
    h.type("su3");
    h.key('<');
    h.key('?');
    check_eq(h.preedit(), "你<?", "default keeps half-width punctuation");

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
    check_eq(quote.preedit(), "「你」", "fullwidth corner quotes");

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
    symbols.key('\'');
    check_eq(symbols.preedit(), "＠＃＄％＆＊＋＝｜～＿｀＂＇",
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
    test_common_mixed_literals();
    test_eten_typing();
    test_hsu_typing();
    test_additional_layout_typing();
    test_layout_switch_resets_preedit();
    test_keypad_literal();
    test_keypad_navigation();
    test_enter_commit();
    test_forced_english_toggle();
    test_forced_english_persists_across_reset();
    test_backspace();
    test_phrase_priority();
    test_live_matches_top_candidate();
    test_phrase_pick();
    test_candidate_direct_selection();
    test_pin_earlier_pick();
    test_insert_chinese_midstring();
    test_paste_at_caret();
    test_midstring_delete_boundaries();
    test_up_navigates_not_revert();
    test_revert_entry();
    test_candidate_paging();
    test_candidate_tab_navigation();
    test_reinterpret();
    test_insert_while_selecting();
    test_commit_after_pick();
    test_selection_backspace();
    test_caret_delete_home_end();
    test_direct_navigation_enters_editing();
    test_escape_behavior();
    test_picking_delete_focused_cell();
    test_fullwidth_punct();
    test_deterministic_key_stress();

    return test::finish();
}
