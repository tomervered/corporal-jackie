cmake_minimum_required(VERSION 3.21)

project(CorporalJackie
    VERSION 0.0.1
    DESCRIPTION "A native media player that streams torrents"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# App identity, used by the code and by packaging.
set(CJ_APP_NAME        "Corporal Jackie")
set(CJ_APP_ID          "corporal-jackie")
set(CJ_ORG_DOMAIN      "tomervered.com")
set(CJ_BUNDLE_ID       "com.tomervered.corporaljackie")

option(CJ_BUILD_TESTS "Build the test suite" ON)
option(CJ_WARNINGS_AS_ERRORS "Fail the build on any compiler warning (CI)" OFF)
option(CJ_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(CJ_SANITIZE_THREAD "Build with ThreadSanitizer (data races)" OFF)
option(CJ_FUZZ "Build the libFuzzer parser target (clang only; implies ASan and UBSan)" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Qml Quick)
include(Dependencies)   # libmpv, libtorrent-rasterbar, SQLite

set(CMAKE_AUTOMOC ON)

# Our own code only: the dependencies are found as installed packages, and
# Qt's headers are system headers, so their warnings do not show.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
    if(CJ_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
    if(CJ_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
    if(CJ_SANITIZE_THREAD)
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    endif()
    if(CJ_FUZZ)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "CJ_FUZZ needs clang (libFuzzer)")
        endif()
        add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
                            -fno-sanitize-recover=undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

add_subdirectory(src)

if(CJ_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
if(CJ_FUZZ)
    add_subdirectory(tests/fuzz)
endif()
