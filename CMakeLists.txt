cmake_minimum_required(VERSION 3.16)
project(inputer VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(INPUTER_ENABLE_SANITIZERS
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(INPUTER_SANITIZER_DETECT_LEAKS
    "Enable LeakSanitizer leak checks when sanitizers are enabled" OFF)
option(INPUTER_ENABLE_FUZZING
    "Build libFuzzer targets for Buffer state-machine fuzzing" OFF)
option(INPUTER_ENABLE_COVERAGE
    "Build tests with compiler coverage instrumentation" OFF)

find_package(ECM REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Config REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5ModuleClipboard REQUIRED) # clipboard_public.h for Ctrl+V paste

find_package(PkgConfig REQUIRED)
pkg_check_modules(Chewing REQUIRED IMPORTED_TARGET chewing)

include(GNUInstallDirs)

function(inputer_enable_warnings target)
    target_compile_options(${target}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    )
endfunction()

function(inputer_enable_sanitizers target)
    if(NOT INPUTER_ENABLE_SANITIZERS)
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

function(inputer_enable_fuzzer target)
    if(NOT INPUTER_ENABLE_FUZZING)
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        message(FATAL_ERROR "INPUTER_ENABLE_FUZZING requires Clang or AppleClang")
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

function(inputer_enable_coverage target)
    if(NOT INPUTER_ENABLE_COVERAGE)
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "INPUTER_ENABLE_COVERAGE requires GCC or Clang")
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

function(inputer_configure_test name)
    if(INPUTER_ENABLE_SANITIZERS AND NOT INPUTER_SANITIZER_DETECT_LEAKS)
        set_tests_properties(${name}
            PROPERTIES
                ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;LSAN_OPTIONS=detect_leaks=0"
        )
    endif()
endfunction()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/data/inputer-addon.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/inputer-addon.conf"
    @ONLY
)

add_library(inputer MODULE
    src/layout.cpp
    src/zhuyin.cpp
    src/buffer.cpp
    src/inputer.cpp
)

target_link_libraries(inputer
    PRIVATE
        Fcitx5::Core
        Fcitx5::Utils
        Fcitx5::Module::Clipboard
        PkgConfig::Chewing
)
inputer_enable_warnings(inputer)
inputer_enable_sanitizers(inputer)

# Drop the "lib" prefix: fcitx5 looks for "<Library>.so" where Library=inputer.
set_target_properties(inputer PROPERTIES PREFIX "")

install(TARGETS inputer
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
)

# Addon descriptor (tells fcitx5 this shared library is an addon).
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/inputer-addon.conf"
    RENAME inputer.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
)

# Input method registration.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/inputer-im.conf"
    RENAME inputer.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/inputer.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)

# --- Tests ---
include(CTest)
if(BUILD_TESTING)
    add_executable(test_buffer
        test/test_buffer.cpp
        src/layout.cpp
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
    inputer_enable_warnings(test_buffer)
    inputer_enable_sanitizers(test_buffer)
    inputer_enable_coverage(test_buffer)
    add_test(NAME buffer COMMAND test_buffer)
    inputer_configure_test(buffer)

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
    inputer_enable_warnings(test_layout)
    inputer_enable_sanitizers(test_layout)
    inputer_enable_coverage(test_layout)
    add_test(NAME layout COMMAND test_layout)
    inputer_configure_test(layout)
endif()

if(INPUTER_ENABLE_FUZZING)
    add_executable(fuzz_buffer
        test/fuzz_buffer.cpp
        src/layout.cpp
        src/buffer.cpp
        src/zhuyin.cpp
    )
    target_include_directories(fuzz_buffer PRIVATE src test)
    target_link_libraries(fuzz_buffer
        PRIVATE
            Fcitx5::Utils
            PkgConfig::Chewing
    )
    inputer_enable_warnings(fuzz_buffer)
    inputer_enable_fuzzer(fuzz_buffer)
endif()
