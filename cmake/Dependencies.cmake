# Finds the native dependencies and exposes one imported target for each:
#   cj::mpv        libmpv (GPL build; see docs/DEPENDENCIES.md)
#   cj::libtorrent libtorrent-rasterbar 2.x
#   cj::sqlite     SQLite 3
#
# Every platform gets them from its package manager (MSYS2 on Windows,
# Homebrew on macOS, apt on Linux). Each lookup tries a CMake package first
# and falls back to pkg-config.

find_package(PkgConfig QUIET)

# --- libmpv -----------------------------------------------------------------
if(PKG_CONFIG_FOUND)
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
find_package(LibtorrentRasterbar 2.0 CONFIG QUIET)
if(TARGET LibtorrentRasterbar::torrent-rasterbar)
    add_library(cj::libtorrent ALIAS LibtorrentRasterbar::torrent-rasterbar)
elseif(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBTORRENT REQUIRED IMPORTED_TARGET libtorrent-rasterbar>=2.0)
    add_library(cj::libtorrent ALIAS PkgConfig::LIBTORRENT)
else()
    message(FATAL_ERROR "libtorrent-rasterbar 2.x not found.")
endif()

# --- SQLite -----------------------------------------------------------------
find_package(SQLite3 REQUIRED)
add_library(cj::sqlite ALIAS SQLite::SQLite3)
