// Assertion-based regression tests for the Buffer state machine. Exits non-zero
// on any failure so it can gate changes.
//
// Build & run:
//   g++ -std=c++20 -I../src -I/usr/include/Fcitx5/Utils \
//     $(pkg-config --cflags chewing) \
//     test_buffer.cpp ../src/buffer.cpp ../src/zhuyin.cpp \
//     -lFcitx5Utils $(pkg-config --libs chewing) -o /tmp/test_buffer && /tmp/test_buffer

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include "buffer.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check_eq(const std::string &actual, const std::string &expected,
              const char *label) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        printf("FAIL %-34s got=\"%s\" want=\"%s\"\n", label, actual.c_str(),
               expected.c_str());
    }
}

void check(bool cond, const char *label) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        printf("FAIL %s\n", label);
    }
}

// A tiny driver that feeds keys into one Buffer and accumulates committed text.
struct Sim {
    Buffer b;
    std::string committed;

    void key(fcitx::KeySym sym) {
        fcitx::Key k(sym);
        KeyResult r = b.handleKey(k);
        if (r.hasCommit) {
            committed += r.commitText;
        }
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
    s.key(FcitxKey_Return);
    check_eq(s.preedit(), "su3", "revert entry explodes 你 -> su3");
}

void test_reinterpret() {
    Sim s;
    s.type("catsu3");     // cats以 (auto peel)
    check_eq(s.preedit(), "cats以", "catsu3 auto-guesses cats以");
    s.key(FcitxKey_Left); // caret between s and 以
    s.key(FcitxKey_Left); // caret before s (so ↑ targets s)
    s.key(FcitxKey_Up);   // reinterpret the s (+ 以) -> 你
    check_eq(s.preedit(), "cat你", "reinterpret recovers cat你");
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
    f.b.setFullWidthPunct(true);
    f.type("su3");
    f.key('<');               // ，
    check_eq(f.preedit(), "你，", "fullwidth comma via <");
    f.key('?');               // ？
    check_eq(f.preedit(), "你，？", "fullwidth question mark");

    // ',' must remain bopomofo even in full-width mode (謝/些 need ㄝ).
    Sim g;
    g.b.setFullWidthPunct(true);
    g.type("xu,4");           // a syllable using ',' = ㄝ
    check(g.preedit() != "x，4" && !g.preedit().empty(),
          "',' stays bopomofo in full-width mode");
}

} // namespace

int main() {
    // Isolate chewing's learned dictionary in a fresh temp dir: tests stay
    // deterministic (no leaked homophone frequencies between runs) and never
    // pollute the user's real ~/.config/inputer dictionary. Must happen before
    // any Buffer/Zhuyin is constructed, since the path is read at chewing_new2.
    const char *kCfg = "/tmp/inputer-test-config";
    std::error_code ec;
    std::filesystem::remove_all(kCfg, ec);
    setenv("XDG_CONFIG_HOME", kCfg, 1);

    test_typing();
    test_keypad_literal();
    test_enter_commit();
    test_backspace();
    test_phrase_priority();
    test_live_matches_top_candidate();
    test_phrase_pick();
    test_pin_earlier_pick();
    test_insert_chinese_midstring();
    test_paste_at_caret();
    test_up_navigates_not_revert();
    test_revert_entry();
    test_reinterpret();
    test_insert_while_selecting();
    test_commit_after_pick();
    test_selection_backspace();
    test_fullwidth_punct();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
