cmake_minimum_required(VERSION 3.16)
project(inputer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(Chewing REQUIRED IMPORTED_TARGET chewing)

include(GNUInstallDirs)

add_library(inputer MODULE
    src/zhuyin.cpp
    src/buffer.cpp
    src/inputer.cpp
)

target_link_libraries(inputer
    PRIVATE
        Fcitx5::Core
        Fcitx5::Utils
        PkgConfig::Chewing
)

# Drop the "lib" prefix: fcitx5 looks for "<Library>.so" where Library=inputer.
set_target_properties(inputer PROPERTIES PREFIX "")

install(TARGETS inputer
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
)

# Addon descriptor (tells fcitx5 this shared library is an addon).
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/inputer-addon.conf"
    RENAME inputer.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
)

# Input method registration.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/inputer-im.conf"
    RENAME inputer.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
)

# --- Tests ---
include(CTest)
if(BUILD_TESTING)
    add_executable(test_buffer
        test/test_buffer.cpp
        src/buffer.cpp
        src/zhuyin.cpp
    )
    target_include_directories(test_buffer PRIVATE src)
    target_link_libraries(test_buffer
        PRIVATE
            Fcitx5::Utils
            PkgConfig::Chewing
    )
    add_test(NAME buffer COMMAND test_buffer)
endif()
