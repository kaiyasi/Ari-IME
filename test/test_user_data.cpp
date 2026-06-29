// Assertion-based regression tests for user dictionary isolation, reset, and
// learning persistence.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include "buffer.h"
#include "test_common.h"
#include "user_data.h"

namespace {

using test::check;
using test::check_eq;

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
    void key(fcitx::KeySym sym) { press(fcitx::Key(sym)); }
    void key(char c) {
        key(static_cast<fcitx::KeySym>(static_cast<unsigned char>(c)));
    }
    void type(const std::string &text) {
        for (char c : text) {
            key(c);
        }
    }
    std::string preedit() const { return b.preeditText(); }
    std::vector<std::string> cand() const { return b.candidates(); }
};

int findVisibleCandidate(const std::vector<std::string> &cands,
                         const std::string &text) {
    for (int i = 0; i < static_cast<int>(cands.size()); ++i) {
        if (cands[i] == text) {
            return i;
        }
    }
    return -1;
}

bool fileExists(const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::uintmax_t fileSize(const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::file_size(path, ec);
}

bool teachSingleChoice(const std::string &candidate) {
    Sim sim;
    sim.type("su3");
    sim.key(FcitxKey_Down);
    const int idx = findVisibleCandidate(sim.cand(), candidate);
    if (idx < 0) {
        return false;
    }
    KeyResult pick = sim.b.selectCandidate(idx);
    if (!pick.handled) {
        return false;
    }
    sim.key(FcitxKey_Return);
    return !sim.committed.empty();
}

void test_test_isolation_disables_learning() {
    test::TempConfigHome configHome("inputer-userdata-isolation-test");
    std::error_code ec;
    check(inputer::ensureUserDataDir(ec), "test isolation can create temp user data dir");
    const std::filesystem::path dict = inputer::userDictionaryPath();
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path();
    check(!inputer::autoLearnEnabled(),
          "ordinary tests keep auto-learning disabled");
    check(dict.native().rfind(tempRoot.native(), 0) == 0,
          "ordinary tests redirect user data into a temp directory");

    check(teachSingleChoice("妳"), "isolated test can still complete a commit");
    check(fileExists(dict), "libchewing may still create an isolated user dictionary file");
    check(fileSize(dict) > 0, "isolated user dictionary artifact stays local to the temp dir");
}

void test_reset_only_clears_user_dictionary() {
    test::TempConfigHome configHome("inputer-userdata-reset-test", false);
    std::error_code ec;
    check(inputer::ensureUserDataDir(ec), "learning test can create temp user data dir");
    const std::filesystem::path dir = inputer::userDataDir();
    const std::filesystem::path sentinel = dir / "base-resource-sentinel.txt";
    {
        std::ofstream out(sentinel);
        out << "keep";
    }
    check(fileExists(sentinel), "sentinel resource exists before reset");

    inputer::resetUserDictionary(ec);
    check(!ec, "reset handles missing dictionary file");
    check(fileExists(sentinel), "reset keeps unrelated resources intact");

    Sim sim;
    sim.type("su3");
    check(!sim.preedit().empty() && sim.preedit() != "su3",
          "reset does not break base dictionary conversion");
}

void test_learning_can_restart_after_reset() {
    test::TempConfigHome configHome("inputer-userdata-learning-test", false);
    std::error_code ec;
    check(inputer::ensureUserDataDir(ec), "learning restart test can create temp dir");
    const std::filesystem::path dict = inputer::userDictionaryPath();
    check(inputer::autoLearnEnabled(),
          "learning restart test runs with auto-learning enabled");

    inputer::resetUserDictionary(ec);
    check(!ec, "initial reset succeeds");
    check(!fileExists(dict), "dictionary starts cleared");

    check(teachSingleChoice("妳"), "learning commit succeeds after clean reset");
    check(fileExists(dict), "learning recreates user dictionary file");
    const std::uintmax_t firstSize = fileSize(dict);
    check(firstSize > 0, "learned dictionary file is non-empty");

    inputer::resetUserDictionary(ec);
    check(!ec, "second reset succeeds");
    check(!fileExists(dict), "reset removes learned dictionary file");

    Sim base;
    base.type("su3");
    check(!base.preedit().empty() && base.preedit() != "su3",
          "base dictionary still converts after learned reset");

    check(teachSingleChoice("妳"), "learning can start again after reset");
    check(fileExists(dict), "dictionary file is recreated after relearning");
    check(fileSize(dict) > 0, "relearned dictionary file is non-empty");
}

} // namespace

int main() {
    test_test_isolation_disables_learning();
    test_reset_only_clears_user_dictionary();
    test_learning_can_restart_after_reset();
    return test::finish();
}
