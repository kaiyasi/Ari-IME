// Assertion-based regression tests for user dictionary isolation, reset, and
// learning persistence.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <chewing.h>

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

bool commitDefault(const std::string &keys) {
    Sim sim;
    sim.type(keys);
    sim.key(FcitxKey_Return);
    return !sim.committed.empty();
}

bool teachPhraseChoice(const std::string &keys, const std::string &candidate) {
    Sim sim;
    sim.type(keys);
    sim.key(FcitxKey_Home);
    sim.key(FcitxKey_Down);
    const int idx = findVisibleCandidate(sim.cand(), candidate);
    if (idx < 0 || !sim.b.selectCandidate(idx).handled) {
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
    const std::filesystem::path preferences = inputer::userPreferencePath();
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path();
    check(!inputer::autoLearnEnabled(),
          "ordinary tests keep auto-learning disabled");
    check(dict.native().rfind(tempRoot.native(), 0) == 0,
          "ordinary tests redirect user data into a temp directory");

    check(teachSingleChoice("妳"), "isolated test can still complete a commit");
    check(fileExists(dict), "libchewing may still create an isolated user dictionary file");
    check(fileSize(dict) > 0, "isolated user dictionary artifact stays local to the temp dir");
    check(!fileExists(preferences),
          "ordinary learning does not create Ari's explicit preference sidecar");
}

void test_reset_only_clears_user_dictionary() {
    test::TempConfigHome configHome("inputer-userdata-reset-test", false);
    std::error_code ec;
    check(inputer::ensureUserDataDir(ec), "learning test can create temp user data dir");
    const std::filesystem::path dir = inputer::userDataDir();
    const std::filesystem::path preferences = inputer::userPreferencePath();
    const std::filesystem::path sentinel = dir / "base-resource-sentinel.txt";
    {
        std::ofstream out(sentinel);
        out << "keep";
    }
    check(fileExists(sentinel), "sentinel resource exists before reset");
    {
        std::ofstream out(preferences);
        out << "妳\n";
    }
    check(fileExists(preferences), "explicit preference sidecar exists before reset");

    inputer::resetUserDictionary(ec);
    check(!ec, "reset handles missing dictionary file");
    check(fileExists(sentinel), "reset keeps unrelated resources intact");
    check(!fileExists(preferences), "reset removes Ari's explicit preference sidecar");
    check(!fileExists(dir / "chewing.dat"),
          "reset removes Ari's standard libchewing dictionary");
    check(!fileExists(dir / "chewing-deleted.dat"),
          "reset removes Ari's deleted-entry dictionary");

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

void test_learning_changes_future_conversion() {
#if defined(CHEWING_VERSION_MAJOR) && defined(CHEWING_VERSION_MINOR) &&           \
    (CHEWING_VERSION_MAJOR > 0 || CHEWING_VERSION_MINOR >= 9)
    test::TempConfigHome configHome("inputer-userdata-ranking-test", false);
    check(inputer::autoLearnEnabled(),
          "candidate ranking test runs with auto-learning enabled");

    Sim before;
    before.type("su3");
    check_eq(before.preedit(), "你", "clean dictionary starts with base conversion");

    check(teachSingleChoice("妳"), "explicit choice commits for learning");

    Sim after;
    after.type("su3");
    check_eq(after.preedit(), "妳",
             "learned homophone becomes the future default conversion");
#endif
}

void test_explicit_phrase_outweighs_accepted_defaults() {
#if defined(CHEWING_VERSION_MAJOR) && defined(CHEWING_VERSION_MINOR) &&           \
    (CHEWING_VERSION_MAJOR > 0 || CHEWING_VERSION_MINOR >= 9)
    test::TempConfigHome configHome("inputer-userdata-weight-test", false);

    for (int i = 0; i < 3; ++i) {
        check(commitDefault("su3cl3"),
              "unchanged contextual default commits as weak evidence");
    }
    check(teachPhraseChoice("su3cl3", "妳好"),
          "explicit phrase choice commits as strong evidence");

    Sim after;
    after.type("su3cl3");
    check_eq(after.preedit(), "妳好",
             "one explicit phrase choice outweighs three accepted defaults");
#endif
}

void test_sensitive_field_does_not_learn() {
#if defined(CHEWING_VERSION_MAJOR) && defined(CHEWING_VERSION_MINOR) &&           \
    (CHEWING_VERSION_MAJOR > 0 || CHEWING_VERSION_MINOR >= 9)
    test::TempConfigHome configHome("inputer-sensitive-learning-test", false);

    {
        Sim sensitive;
        sensitive.b.setLearningAllowed(false);
        sensitive.type("su3");
        sensitive.key(FcitxKey_Down);
        const int idx = findVisibleCandidate(sensitive.cand(), "妳");
        check(idx >= 0, "sensitive learning setup includes 妳");
        check(sensitive.b.selectCandidate(idx).handled,
              "sensitive field can still choose a candidate");
        sensitive.key(FcitxKey_Return);
    }

    {
        Sim afterSensitive;
        afterSensitive.type("su3");
        check_eq(afterSensitive.preedit(), "你",
                 "sensitive-field candidate choice is not learned");
    }

    check(teachSingleChoice("妳"),
          "ordinary field still learns after sensitive commit");
    Sim afterOrdinary;
    afterOrdinary.type("su3");
    check_eq(afterOrdinary.preedit(), "妳",
             "ordinary-field learning remains enabled");
#endif
}

void test_forget_personal_candidate() {
#if defined(CHEWING_VERSION_MAJOR) && defined(CHEWING_VERSION_MINOR) &&           \
    (CHEWING_VERSION_MAJOR > 0 || CHEWING_VERSION_MINOR >= 9)
    test::TempConfigHome configHome("inputer-forget-learning-test", false);
    check(teachSingleChoice("妳"), "forget setup learns 妳");

    {
        Sim learned;
        learned.type("su3");
        check_eq(learned.preedit(), "妳", "forget setup promotes learned candidate");
        learned.key(FcitxKey_Down);
        KeyResult forgotten = learned.press(fcitx::Key(
            FcitxKey_Delete,
            fcitx::KeyStates{fcitx::KeyState::Shift}));
        check(forgotten.handled, "Shift+Delete handles candidate forgetting");
        check(forgotten.notification.find("已忘記") == 0,
              "candidate forgetting reports success");
    }

    Sim afterForget;
    afterForget.type("su3");
    check_eq(afterForget.preedit(), "你",
             "forgotten personal candidate no longer wins conversion");
#endif
}

void test_userphrase_mapping_can_be_restored() {
    test::TempConfigHome configHome("inputer-userphrase-import-test", false);

    {
        Zhuyin engine;
        check(engine.addUserPhrase("妳", "ㄋㄧˇ") > 0,
              "portable userphrase mapping can be added");
        check(engine.addUserPhrase("資", "ㄗ") > 0,
              "portable one-key tone-one mapping can be added");
        check(fileExists(inputer::userPreferencePath()),
              "portable mapping creates Ari's explicit preference sidecar");
        const auto entries = engine.userPhrases();
        bool found = false;
        for (const auto &entry : entries) {
            if (entry.phrase == "妳" && entry.reading == "ㄋㄧˇ") {
                found = true;
                break;
            }
        }
        check(found, "portable userphrase mapping can be enumerated");
    }

    Sim restored;
    restored.type("su3");
    check_eq(restored.preedit(), "妳",
             "imported userphrase mapping is promoted to the live result");
    restored.key(FcitxKey_Down);
    check(findVisibleCandidate(restored.cand(), "妳") >= 0,
          "imported userphrase mapping remains selectable");

    Sim single;
    single.key('y');
    single.key(FcitxKey_space);
    check_eq(single.preedit(), "資",
             "imported one-key mapping wins after tone-one completion");
}

void test_legacy_dictionary_is_seeded_for_libchewing() {
    test::TempConfigHome configHome("inputer-userdata-migration-test", false);
    std::error_code ec;
    const std::filesystem::path dir = inputer::userDataDir();
    std::filesystem::create_directories(dir, ec);
    check(!ec, "migration test creates user data directory");

    const std::filesystem::path legacy = dir / "userdict.dat";
    const std::filesystem::path standard = dir / "chewing.dat";
    check(teachSingleChoice("妳"),
          "migration test creates a valid learned dictionary");
    check(fileExists(legacy), "migration setup has legacy userdict.dat");
    std::filesystem::remove(standard, ec);
    check(!ec, "migration setup removes only the standard-name copy");

    check(inputer::ensureUserDataDir(ec), "legacy dictionary migration succeeds");
    check(fileExists(standard), "migration seeds chewing.dat");
    check(fileExists(legacy), "migration preserves userdict.dat");
    check(fileSize(standard) == fileSize(legacy),
          "migrated dictionary preserves legacy contents");
}

} // namespace

int main() {
    test_test_isolation_disables_learning();
    test_reset_only_clears_user_dictionary();
    test_learning_can_restart_after_reset();
    test_learning_changes_future_conversion();
    test_explicit_phrase_outweighs_accepted_defaults();
    test_sensitive_field_does_not_learn();
    test_forget_personal_candidate();
    test_userphrase_mapping_can_be_restored();
    test_legacy_dictionary_is_seeded_for_libchewing();
    return test::finish();
}
