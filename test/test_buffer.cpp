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

void test_phrase_priority() {
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left); // enter, cursor on 好
    s.key(FcitxKey_Left); // cursor on 你
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
    s.key(FcitxKey_Left); // enter selection (last char)
    s.key(FcitxKey_Left); // cursor on 你
    auto c = s.cand();
    check(!c.empty() && c[0] == "你好", "selection top candidate matches live");
}

void test_phrase_pick() {
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left);
    s.key(FcitxKey_Left); // cursor on 你
    s.key('2');           // pick #2 = 妳好 (phrase)
    check_eq(s.preedit(), "妳好", "phrase pick rewrites both cells");
}

void test_pin_earlier_pick() {
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left);
    s.key(FcitxKey_Left);  // cursor on 你
    s.key(FcitxKey_Down);  // 妳好
    s.key(FcitxKey_Down);  // 你 (single)
    s.key(FcitxKey_Down);  // 妳 (single)
    s.key(FcitxKey_Return);
    check_eq(s.preedit(), "妳好", "picked 妳 single");
    check(s.b.selectionChar() == -1, "selection ends after pick");
    // Re-open selection to fix 好: the earlier 妳 pick must stay locked.
    s.key(FcitxKey_Left);  // enter selection on the last cell (好)
    s.key(FcitxKey_Down);  // 郝
    s.key(FcitxKey_Return);
    check_eq(s.preedit(), "妳郝", "earlier 妳 stays locked after picking 郝");
}

void test_up_navigates_not_revert() {
    Sim s;
    s.type("g4"); // 是 (no phrase)
    s.key(FcitxKey_Left);
    s.key(FcitxKey_Down); // hl1
    s.key(FcitxKey_Up);   // back to hl0 — must NOT revert to english
    check_eq(s.preedit(), "是", "Up navigates candidates, no revert");
}

void test_revert_entry() {
    Sim s;
    s.type("su3");        // 你
    s.key(FcitxKey_Left); // cursor on 你, highlight 0
    s.key(FcitxKey_Up);   // wrap to the last entry = raw-keys revert
    s.key(FcitxKey_Return);
    check_eq(s.preedit(), "su3", "revert entry explodes 你 -> su3");
}

void test_reinterpret() {
    Sim s;
    s.type("catsu3");     // cats以 (auto peel)
    check_eq(s.preedit(), "cats以", "catsu3 auto-guesses cats以");
    s.key(FcitxKey_Left); // cursor on 以
    s.key(FcitxKey_Left); // cursor on s (english)
    s.key(FcitxKey_Up);   // reinterpret s+u3 -> 你
    check_eq(s.preedit(), "cat你", "reinterpret recovers cat你");
}

void test_insert_while_selecting() {
    Sim s;
    s.type("fie");
    s.key(FcitxKey_Left); // cursor on e
    s.key('l');           // insert before e
    check_eq(s.preedit(), "file", "insert l before e -> file");
}

void test_commit_after_pick() {
    // Picking then committing must output exactly the current pre-edit (the
    // learning replay must not alter it). A pick ends selection; Enter commits.
    Sim s;
    s.type("su3cl3");
    s.key(FcitxKey_Left);
    s.key(FcitxKey_Left);     // cursor on 你
    s.key('2');               // pick the 2nd candidate (a phrase) -> ends selection
    std::string chosen = s.preedit();
    s.key(FcitxKey_Return);   // commit
    check_eq(s.committed, chosen, "commit reflects current pre-edit");
    check_eq(s.preedit(), "", "preedit cleared");
}

void test_selection_backspace() {
    Sim s;
    s.type("su3cl3");          // 你好
    s.key(FcitxKey_Left);      // enter selection on 好
    s.key(FcitxKey_BackSpace); // delete focused char and leave selection
    check_eq(s.preedit(), "你", "selection backspace deletes focused char");
    check(s.b.selectionChar() == -1, "selection ends after backspace");
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
