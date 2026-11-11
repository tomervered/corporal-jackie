# Finds the native dependencies and exposes one imported target for each:
#   cj::mpv        libmpv (GPL build; see docs/DEPENDENCIES.md)
#   cj::libtorrent libtorrent-rasterbar 2.x
#   cj::sqlite     SQLite 3
#
# Every platform gets them from its package manager (MSYS2 on Windows,
# Homebrew on macOS, apt on Linux), except libtorrent (patched, see below). Each lookup tries a CMake package first
# and falls back to pkg-config.

find_package(PkgConfig QUIET)

# --- libmpv -----------------------------------------------------------------
# On Windows (MSYS2) mpv's pkg-config file reports MSYS-style paths such as
# /ucrt64/include that native CMake cannot use, so look the files up directly.
if(PKG_CONFIG_FOUND AND NOT WIN32)
    pkg_check_modules(MPV QUIET IMPORTED_TARGET mpv)
endif()
if(TARGET PkgConfig::MPV)
    add_library(cj::mpv ALIAS PkgConfig::MPV)
else()
    find_path(MPV_INCLUDE_DIR mpv/client.h)
    find_library(MPV_LIBRARY NAMES mpv libmpv mpv-2)
    if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
        message(FATAL_ERROR "libmpv not found. Install mpv's development files "
                            "or set CMAKE_PREFIX_PATH to a libmpv dev package.")
    endif()
    add_library(cj_mpv INTERFACE)
    target_include_directories(cj_mpv INTERFACE "${MPV_INCLUDE_DIR}")
    target_link_libraries(cj_mpv INTERFACE "${MPV_LIBRARY}")
    add_library(cj::mpv ALIAS cj_mpv)
endif()

# --- libtorrent-rasterbar ---------------------------------------------------
# The app wants third_party/libtorrent/build.sh's build: libtorrent with one
# patch that stops playback stalling when a seeder leaves (docs/ENGINE.md).
# Put its prefix in CMAKE_PREFIX_PATH. Any other libtorrent 2.x still works,
# without the patch, and the configure step says so.
option(CJ_REQUIRE_PATCHED_LIBTORRENT "Fail unless libtorrent is the patched build from third_party/libtorrent" OFF)
find_package(LibtorrentRasterbar 2.0 CONFIG QUIET)
if(TARGET LibtorrentRasterbar::torrent-rasterbar)
    add_library(cj::libtorrent ALIAS LibtorrentRasterbar::torrent-rasterbar)
    get_filename_component(_cj_lt_prefix "${LibtorrentRasterbar_DIR}/../../.." ABSOLUTE)
    file(GLOB _cj_lt_stamp "${_cj_lt_prefix}/share/corporal-jackie/libtorrent-[0-9]*")
elseif(PKG_CONFIG_FOUND AND NOT CJ_REQUIRE_PATCHED_LIBTORRENT)
    pkg_check_modules(LIBTORRENT REQUIRED IMPORTED_TARGET libtorrent-rasterbar>=2.0)
    add_library(cj::libtorrent ALIAS PkgConfig::LIBTORRENT)
else()
    message(FATAL_ERROR "libtorrent-rasterbar 2.x not found. Build ours with "
                        "third_party/libtorrent/build.sh <prefix> and add <prefix> to CMAKE_PREFIX_PATH.")
endif()
if(_cj_lt_stamp)
    message(STATUS "libtorrent: patched build in ${_cj_lt_prefix}")
    # The installed target asks a Debug consumer to define TORRENT_USE_ASSERTS,
    # which adds members to libtorrent's classes. The library is built once,
    # without it, so a Debug app (the sanitizer jobs) would disagree with it
    # about the layout of torrent_status and others.
    get_target_property(_cj_lt_defs LibtorrentRasterbar::torrent-rasterbar INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER _cj_lt_defs EXCLUDE REGEX "TORRENT_USE_ASSERTS")
    set_property(TARGET LibtorrentRasterbar::torrent-rasterbar PROPERTY INTERFACE_COMPILE_DEFINITIONS "${_cj_lt_defs}")
elseif(CJ_REQUIRE_PATCHED_LIBTORRENT)
    message(FATAL_ERROR "This libtorrent is not the patched build from third_party/libtorrent/build.sh.")
else()
    message(WARNING "libtorrent is not the patched build (third_party/libtorrent/build.sh): "
                    "playback stalls longer when a seeder leaves.")
endif()

# --- SQLite -----------------------------------------------------------------
find_package(SQLite3 REQUIRED)
if(TARGET SQLite3::SQLite3)          # CMake 4.1 and newer
    add_library(cj::sqlite ALIAS SQLite3::SQLite3)
else()
    add_library(cj::sqlite ALIAS SQLite::SQLite3)
endif()
