#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
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
    explicit TempConfigHome(const std::string &name, bool disableAutoLearn = true)
        : savedXdg_(captureEnv("XDG_CONFIG_HOME")),
          savedDisableLearn_(captureEnv("INPUTER_DISABLE_AUTOLEARN")),
          savedUserDataDir_(captureEnv("INPUTER_USER_DATA_DIR")) {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                (name + "-" + std::to_string(getpid()) + "-" +
                 std::to_string(counter++));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
        setenv("XDG_CONFIG_HOME", path_.c_str(), 1);
        userDataDir_ = path_ / "inputer";
        setenv("INPUTER_USER_DATA_DIR", userDataDir_.c_str(), 1);
        if (disableAutoLearn) {
            setenv("INPUTER_DISABLE_AUTOLEARN", "1", 1);
        } else {
            unsetenv("INPUTER_DISABLE_AUTOLEARN");
        }
    }

    ~TempConfigHome() {
        restoreEnv("XDG_CONFIG_HOME", savedXdg_);
        restoreEnv("INPUTER_DISABLE_AUTOLEARN", savedDisableLearn_);
        restoreEnv("INPUTER_USER_DATA_DIR", savedUserDataDir_);
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempConfigHome(const TempConfigHome &) = delete;
    TempConfigHome &operator=(const TempConfigHome &) = delete;

private:
    static std::optional<std::string> captureEnv(const char *name) {
        if (const char *value = std::getenv(name); value) {
            return std::string(value);
        }
        return std::nullopt;
    }

    static void restoreEnv(const char *name,
                           const std::optional<std::string> &value) {
        if (value) {
            setenv(name, value->c_str(), 1);
        } else {
            unsetenv(name);
        }
    }

    std::filesystem::path path_;
    std::filesystem::path userDataDir_;
    std::optional<std::string> savedXdg_;
    std::optional<std::string> savedDisableLearn_;
    std::optional<std::string> savedUserDataDir_;
};

} // namespace test
