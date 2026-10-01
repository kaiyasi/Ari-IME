cmake_minimum_required(VERSION 3.16)
project(ari-ime VERSION 2.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ARI_IME_ENABLE_SANITIZERS
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ARI_IME_SANITIZER_DETECT_LEAKS
    "Enable LeakSanitizer leak checks when sanitizers are enabled" OFF)
option(ARI_IME_ENABLE_FUZZING
    "Build libFuzzer targets for Buffer state-machine fuzzing" OFF)
option(ARI_IME_ENABLE_COVERAGE
    "Build tests with compiler coverage instrumentation" OFF)
option(BUILD_TESTING
    "Build maintainer-only local tests" OFF)

find_package(ECM REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Config REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5ModuleClipboard REQUIRED) # clipboard_public.h for Ctrl+V paste

# showCustomInputMethodInformation() was added in Fcitx5 5.1.11. Keep the
# addon buildable with the older Fcitx5 shipped by Ubuntu 24.04; the fallback
# uses the standard information popup and preserves the input behavior.
set(ARI_IME_HAVE_CUSTOM_INPUT_METHOD_INFORMATION OFF)
if(DEFINED Fcitx5Core_VERSION AND
   NOT Fcitx5Core_VERSION VERSION_LESS "5.1.11")
    set(ARI_IME_HAVE_CUSTOM_INPUT_METHOD_INFORMATION ON)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(Chewing REQUIRED IMPORTED_TARGET chewing)
message(STATUS "Using chewing ${Chewing_VERSION}")

# libchewing 0.6 (the version shipped by Ubuntu 24.04) predates the explicit
# output acknowledgement API.  Its compatibility wrapper needs a harmless
# ignored event after the commit buffer has been consumed instead.
if(Chewing_VERSION VERSION_LESS "0.9.0")
    add_compile_definitions(ARI_IME_LIBCHEWING_LEGACY_OUTPUT)
endif()

# libchewing 0.10 made explicit user phrases take precedence. Older releases
# expose the mapping to the C API but keep it in dictionary order, so Ari must
# apply its own explicit preference sidecar for those distributions.
if(Chewing_VERSION VERSION_LESS "0.10.0")
    add_compile_definitions(ARI_IME_LEGACY_PREFERENCE_PROMOTION)
endif()

include(GNUInstallDirs)

function(ari_ime_enable_warnings target)
    target_compile_options(${target}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    )
endfunction()

function(ari_ime_enable_sanitizers target)
    if(NOT ARI_IME_ENABLE_SANITIZERS)
        return()
    endif()
    target_compile_options(${target}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined -fno-omit-frame-pointer>
    )
    target_link_options(${target}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined>
    )
endfunction()

function(ari_ime_enable_fuzzer target)
    if(NOT ARI_IME_ENABLE_FUZZING)
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        message(FATAL_ERROR "ARI_IME_ENABLE_FUZZING requires Clang or AppleClang")
    endif()
    target_compile_options(${target}
        PRIVATE
            -fsanitize=fuzzer,address,undefined
            -fno-omit-frame-pointer
    )
    target_link_options(${target}
        PRIVATE
            -fsanitize=fuzzer,address,undefined
    )
endfunction()

function(ari_ime_enable_coverage target)
    if(NOT ARI_IME_ENABLE_COVERAGE)
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "ARI_IME_ENABLE_COVERAGE requires GCC or Clang")
    endif()
    target_compile_options(${target}
        PRIVATE
            --coverage
            -O0
            -g
    )
    target_link_options(${target}
        PRIVATE
            --coverage
    )
endfunction()

function(ari_ime_configure_test name)
    if(ARI_IME_ENABLE_SANITIZERS AND NOT ARI_IME_SANITIZER_DETECT_LEAKS)
        set_tests_properties(${name}
            PROPERTIES
                ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;LSAN_OPTIONS=detect_leaks=0"
        )
    endif()
endfunction()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/ari-ime-addon.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ari-ime-addon.conf"
    @ONLY
)

add_library(ari-ime MODULE
    src/layout.cpp
    src/user_data.cpp
    src/zhuyin.cpp
    src/buffer.cpp
    src/ari_ime.cpp
)

target_link_libraries(ari-ime
    PRIVATE
        Fcitx5::Core
        Fcitx5::Utils
        Fcitx5::Module::Clipboard
        PkgConfig::Chewing
)
if(ARI_IME_HAVE_CUSTOM_INPUT_METHOD_INFORMATION)
    target_compile_definitions(ari-ime
        PRIVATE ARI_IME_HAVE_CUSTOM_INPUT_METHOD_INFORMATION
    )
endif()
ari_ime_enable_warnings(ari-ime)
ari_ime_enable_sanitizers(ari-ime)

# Drop the "lib" prefix: fcitx5 looks for "<Library>.so" where Library=ari-ime.
set_target_properties(ari-ime PROPERTIES PREFIX "")

install(TARGETS ari-ime
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
)

# Addon descriptor (tells fcitx5 this shared library is an addon).
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ari-ime-addon.conf"
    RENAME ari-ime.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
)

# Input method registration.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/ari-ime-im.conf"
    RENAME ari-ime.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/ari-ime.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)

# Explicit post-install helpers. They are commands, not package hooks: users
# can opt into adding Ari to their Fcitx5 profile, and can reset Ari-owned
# learning data without needing a source checkout.
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/enable-input-method.sh"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME ari-ime-enable
)
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reset-user-data.sh"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME ari-ime-reset-data
)

# Portable personal-dictionary export/import and diagnostics. This is an
# explicit command-line tool; package installation never mutates the user's
# Fcitx5 profile or dictionary.
add_executable(ari-ime-dict
    src/dict_tool.cpp
    src/layout.cpp
    src/user_data.cpp
    src/zhuyin.cpp
)
target_include_directories(ari-ime-dict PRIVATE src)
target_link_libraries(ari-ime-dict
    PRIVATE
        Fcitx5::Utils
        Fcitx5::Config
        PkgConfig::Chewing
)
target_compile_definitions(ari-ime-dict
    PRIVATE ARI_IME_CHEWING_VERSION="${Chewing_VERSION}"
)
ari_ime_enable_warnings(ari-ime-dict)
ari_ime_enable_sanitizers(ari-ime-dict)
install(TARGETS ari-ime-dict
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# --- Maintainer-only local tests ---
# Buffer regression may be published alone for review; other suite members stay
# optional and are only wired when their sources exist.
set(ARI_IME_LOCAL_TESTS_AVAILABLE FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/test_buffer.cpp")
    set(ARI_IME_LOCAL_TESTS_AVAILABLE TRUE)
endif()

if(BUILD_TESTING AND NOT ARI_IME_LOCAL_TESTS_AVAILABLE)
    message(WARNING
        "BUILD_TESTING=ON was requested, but maintainer-only test sources "
        "are not present; local tests are disabled")
    set(BUILD_TESTING OFF)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_buffer
        test/test_buffer.cpp
        src/layout.cpp
        src/user_data.cpp
        src/buffer.cpp
        src/zhuyin.cpp
    )
    target_include_directories(test_buffer PRIVATE src)
    target_link_libraries(test_buffer
        PRIVATE
            Fcitx5::Utils
            Fcitx5::Config
            PkgConfig::Chewing
    )
    ari_ime_enable_warnings(test_buffer)
    ari_ime_enable_sanitizers(test_buffer)
    ari_ime_enable_coverage(test_buffer)
    add_test(NAME buffer COMMAND test_buffer)
    ari_ime_configure_test(buffer)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/test_layout.cpp")
        add_executable(test_layout
            test/test_layout.cpp
            src/layout.cpp
        )
        target_include_directories(test_layout PRIVATE src)
        target_link_libraries(test_layout
            PRIVATE
                Fcitx5::Config
                PkgConfig::Chewing
        )
        ari_ime_enable_warnings(test_layout)
        ari_ime_enable_sanitizers(test_layout)
        ari_ime_enable_coverage(test_layout)
        add_test(NAME layout COMMAND test_layout)
        ari_ime_configure_test(layout)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/test_user_data.cpp")
        add_executable(test_user_data
            test/test_user_data.cpp
            src/layout.cpp
            src/user_data.cpp
            src/buffer.cpp
            src/zhuyin.cpp
        )
        target_include_directories(test_user_data PRIVATE src)
        target_link_libraries(test_user_data
            PRIVATE
                Fcitx5::Utils
                Fcitx5::Config
                PkgConfig::Chewing
        )
        ari_ime_enable_warnings(test_user_data)
        ari_ime_enable_sanitizers(test_user_data)
        ari_ime_enable_coverage(test_user_data)
        add_test(NAME user_data COMMAND test_user_data)
        ari_ime_configure_test(user_data)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/test_enable_input_method.sh")
        add_test(
            NAME profile_helper
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test/test_enable_input_method.sh"
        )
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/test_dict_tool.sh")
        add_test(
            NAME dict_tool
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test/test_dict_tool.sh"
                    "$<TARGET_FILE:ari-ime-dict>"
        )
        ari_ime_configure_test(dict_tool)
    endif()
endif()

if(ARI_IME_ENABLE_FUZZING AND EXISTS
   "${CMAKE_CURRENT_SOURCE_DIR}/test/fuzz_buffer.cpp")
    add_executable(fuzz_buffer
        test/fuzz_buffer.cpp
        src/layout.cpp
        src/user_data.cpp
        src/buffer.cpp
        src/zhuyin.cpp
    )
    target_include_directories(fuzz_buffer PRIVATE src test)
    target_link_libraries(fuzz_buffer
        PRIVATE
            Fcitx5::Utils
            Fcitx5::Config
            PkgConfig::Chewing
    )
    ari_ime_enable_warnings(fuzz_buffer)
    ari_ime_enable_fuzzer(fuzz_buffer)
elseif(ARI_IME_ENABLE_FUZZING)
    message(WARNING
        "ARI_IME_ENABLE_FUZZING was requested, but maintainer-only fuzz "
        "sources are not present; the fuzz target is disabled")
endif()
