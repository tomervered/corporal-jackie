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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)
include(Dependencies)   # libmpv, libtorrent-rasterbar, SQLite

set(CMAKE_AUTOMOC ON)

add_subdirectory(src)

if(CJ_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
