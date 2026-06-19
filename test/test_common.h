#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>

namespace test {

inline int failures = 0;
inline int checks = 0;

inline void check_eq(const std::string &actual, const std::string &expected,
                     const char *label) {
    ++checks;
    if (actual != expected) {
        ++failures;
        printf("FAIL %-34s got=\"%s\" want=\"%s\"\n", label, actual.c_str(),
               expected.c_str());
    }
}

inline void check(bool cond, const char *label) {
    ++checks;
    if (!cond) {
        ++failures;
        printf("FAIL %s\n", label);
    }
}

inline int finish() {
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

class TempConfigHome {
public:
    explicit TempConfigHome(const std::string &name) {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                (name + "-" + std::to_string(getpid()) + "-" +
                 std::to_string(counter++));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
        setenv("XDG_CONFIG_HOME", path_.c_str(), 1);
        setenv("INPUTER_DISABLE_AUTOLEARN", "1", 1);
    }

    ~TempConfigHome() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempConfigHome(const TempConfigHome &) = delete;
    TempConfigHome &operator=(const TempConfigHome &) = delete;

private:
    std::filesystem::path path_;
};

} // namespace test
