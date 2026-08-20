cmake_minimum_required(VERSION 3.16)
project(ari_ime_wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ARI_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(CHEWING_INCLUDE_DIR "" CACHE PATH
    "Directory containing chewing.h from the WASM libchewing build")
set(CHEWING_LIBRARY "" CACHE FILEPATH
    "Path to the WASM libchewing static library")
set(CHEWING_DATA_DIR "" CACHE PATH
    "Directory containing libchewing's runtime dictionary files")
option(ARI_WASM_BUILD_NATIVE_TEST
       "Build the headless API test with the host compiler" OFF)

if(NOT CHEWING_INCLUDE_DIR)
    find_path(CHEWING_INCLUDE_DIR chewing.h PATH_SUFFIXES chewing)
endif()
if(NOT CHEWING_LIBRARY)
    find_library(CHEWING_LIBRARY NAMES chewing libchewing)
endif()
if(NOT CHEWING_INCLUDE_DIR OR NOT CHEWING_LIBRARY)
    message(FATAL_ERROR
        "Set CHEWING_INCLUDE_DIR and CHEWING_LIBRARY to a libchewing build "
        "for the selected target")
endif()

set(ARI_HEADLESS_SOURCES
    "${ARI_SOURCE_ROOT}/src/layout.cpp"
    "${ARI_SOURCE_ROOT}/src/zhuyin.cpp"
    "${ARI_SOURCE_ROOT}/src/buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/user_data.cpp"
)

add_library(ari-ime-headless STATIC ${ARI_HEADLESS_SOURCES})
target_include_directories(ari-ime-headless
    BEFORE PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${ARI_SOURCE_ROOT}/src"
        "${CHEWING_INCLUDE_DIR}"
)
target_compile_definitions(ari-ime-headless PRIVATE INPUTER_WASM)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_definitions(ari-ime-headless PRIVATE
        # The reproducible WASM build uses libchewing 0.8's portable C
        # backend. That API predates chewing_ack(), so use the compatibility
        # rollover path.
        INPUTER_LIBCHEWING_LEGACY_OUTPUT
    )
endif()
target_compile_options(ari-ime-headless PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ari-ime-headless PUBLIC "${CHEWING_LIBRARY}")

if(ARI_WASM_BUILD_NATIVE_TEST)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/test/native_api.cpp")
        message(FATAL_ERROR
            "ARI_WASM_BUILD_NATIVE_TEST requires the local maintainer-only "
            "wasm/test/native_api.cpp source")
    endif()
    target_compile_definitions(ari-ime-headless
        PRIVATE INPUTER_WASM_NATIVE_TEST)
    add_executable(ari-ime-wasm-native-test
        "${CMAKE_CURRENT_LIST_DIR}/api.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/native_api.cpp")
    target_include_directories(ari-ime-wasm-native-test
        PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${ARI_SOURCE_ROOT}/src")
    target_link_libraries(ari-ime-wasm-native-test PRIVATE ari-ime-headless)
    enable_testing()
    add_test(NAME ari-ime-wasm-native COMMAND ari-ime-wasm-native-test)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(ari-ime-wasm "${CMAKE_CURRENT_LIST_DIR}/api.cpp")
    target_include_directories(ari-ime-wasm
        BEFORE PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${ARI_SOURCE_ROOT}/src"
    )
    target_link_libraries(ari-ime-wasm PRIVATE ari-ime-headless)
    set_target_properties(ari-ime-wasm PROPERTIES
        OUTPUT_NAME "ari-ime-wasm"
        SUFFIX ".js"
    )
    target_link_options(ari-ime-wasm PRIVATE
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=ARIWasmModule"
        "-sENVIRONMENT=web,node"
        "-sNO_EXIT_RUNTIME=1"
        "-sALLOW_MEMORY_GROWTH=1"
        # libchewing's legacy tree loader uses deep recursion while opening
        # the bundled dictionary; Emscripten's 64 KiB default stack is too
        # small for that one-time initialization.
        "-sSTACK_SIZE=1048576"
        "-sFILESYSTEM=1"
        "-sEXPORTED_FUNCTIONS=['_ari_engine_create','_ari_engine_destroy','_ari_engine_state_json','_ari_engine_handle_key_json','_ari_engine_select_candidate_json','_ari_engine_paste_utf8_json','_ari_engine_reconvert_utf8_json','_ari_engine_reset_json','_ari_engine_set_learning_allowed','_ari_engine_set_keyboard_layout','_ari_engine_set_full_width_punctuation','_ari_engine_set_punctuation_shortcut','_ari_engine_set_space_candidate_mode','_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','lengthBytesUTF8','stringToUTF8','FS']"
    )
    if(CHEWING_DATA_DIR)
        target_link_options(ari-ime-wasm PRIVATE
            "--preload-file=${CHEWING_DATA_DIR}@/usr/share/libchewing")
    else()
        message(WARNING
            "CHEWING_DATA_DIR is empty; the generated module will not have "
            "libchewing's dictionary files")
    endif()
endif()
