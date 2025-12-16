cmake_minimum_required(VERSION 3.16)
project(dawn C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_LIBAI "Build with Apple Intelligence (macOS 26+)" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
set(DAWN_VERSION "dev" CACHE STRING "Version string")

# Sanitizer flags
set(SANITIZER_FLAGS "")
if(ENABLE_ASAN)
    list(APPEND SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
endif()
if(ENABLE_UBSAN)
    list(APPEND SANITIZER_FLAGS -fsanitize=undefined)
endif()

file(GLOB DAWN_SRCS src/dawn*.c)
list(FILTER DAWN_SRCS EXCLUDE REGEX "dawn_backend_.*\\.c$")
file(GLOB HIGHLIGHT_SRCS src/highlight/*.c src/highlight/lang/*.c)

set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
    set(PCRE2_SUPPORT_JIT OFF CACHE BOOL "" FORCE)
else()
    set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third-party/pcre2)

add_subdirectory(third-party/utf8proc)

if(NOT EMSCRIPTEN)
    find_package(CURL REQUIRED)

    add_executable(dawn
        ${DAWN_SRCS}
        ${HIGHLIGHT_SRCS}
        src/dawn_backend_posix.c
        src/main_term.c
        third-party/cJSON.c
    )

    target_include_directories(dawn PRIVATE
        src
        third-party
        ${CMAKE_BINARY_DIR}/third-party/pcre2
        third-party/pcre2/src
    )

    target_compile_options(dawn PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wunused
        $<$<C_COMPILER_ID:AppleClang,Clang>:-Wno-keyword-macro -Wno-newline-eof -Wno-deprecated-declarations>
        ${SANITIZER_FLAGS}
    )
    target_compile_definitions(dawn PRIVATE VERSION="${DAWN_VERSION}")

    if(SANITIZER_FLAGS)
        target_link_options(dawn PRIVATE ${SANITIZER_FLAGS})
    endif()

    target_link_libraries(dawn PRIVATE pcre2-8-static utf8proc CURL::libcurl m)

    if(APPLE)
        target_link_libraries(dawn PRIVATE "-framework ApplicationServices")
    endif()

    if(APPLE AND USE_LIBAI)
        target_sources(dawn PRIVATE src/search.c)
        target_compile_definitions(dawn PRIVATE USE_LIBAI)
        target_include_directories(dawn PRIVATE libai)
        target_compile_options(dawn PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable)

        add_custom_target(libai_build
            COMMAND make static-rel
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/libai
        )
        add_dependencies(dawn libai_build)

        target_link_libraries(dawn PRIVATE
            ${CMAKE_SOURCE_DIR}/libai/build/static/${CMAKE_SYSTEM_PROCESSOR}/release/libai.a
            "-framework Foundation"
            "-framework FoundationModels"
            -L/usr/lib/swift -lswiftCore
        )
    endif()

    install(TARGETS dawn RUNTIME DESTINATION bin)

    # Test target for dawn_block
    add_executable(test-block
        tests/test_block.c
        src/dawn_block.c
        src/dawn_gap.c
        src/dawn_md.c
        src/dawn_wrap.c
        third-party/cJSON.c
    )

    target_include_directories(test-block PRIVATE
        src
        third-party
        ${CMAKE_BINARY_DIR}/third-party/pcre2
        third-party/pcre2/src
    )

    target_compile_options(test-block PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wunused
        $<$<C_COMPILER_ID:AppleClang,Clang>:-Wno-keyword-macro -Wno-newline-eof -Wno-deprecated-declarations>
        ${SANITIZER_FLAGS}
    )

    if(SANITIZER_FLAGS)
        target_link_options(test-block PRIVATE ${SANITIZER_FLAGS})
    endif()

    target_link_libraries(test-block PRIVATE pcre2-8-static utf8proc m)
else()
    file(GLOB WEB_SRCS src/dawn*.c)
    list(FILTER WEB_SRCS EXCLUDE REGEX "dawn_args.c")
    list(FILTER WEB_SRCS EXCLUDE REGEX "dawn_backend_posix.c")

    add_executable(dawn-web
        ${WEB_SRCS}
        ${HIGHLIGHT_SRCS}
        src/dawn_backend_web.c
        src/main_web.c
        third-party/cJSON.c
    )

    target_include_directories(dawn-web PRIVATE
        src
        third-party
        ${CMAKE_BINARY_DIR}/third-party/pcre2
        third-party/pcre2/src
    )

    target_compile_options(dawn-web PRIVATE -O2)
    target_link_libraries(dawn-web PRIVATE pcre2-8-static utf8proc)

    set_target_properties(dawn-web PROPERTIES
        OUTPUT_NAME "dawn"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
    )

    target_link_options(dawn-web PRIVATE
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sASYNCIFY
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,setValue,getValue
        -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_web_on_resize,_web_on_key,_web_on_mouse,_dawn_web_load_file,_dawn_web_new_document,_dawn_web_save
        -sFORCE_FILESYSTEM=1
        -lidbfs.js
        --shell-file ${CMAKE_SOURCE_DIR}/src/shell.html
    )
endif()
