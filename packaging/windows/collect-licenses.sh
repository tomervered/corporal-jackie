#!/usr/bin/env bash
# Adds the third-party notices, every shipped library's license files and
# SOURCE.txt to the Windows package. Runs in MSYS2 UCRT64 after the package
# folder is complete.
#
# Usage: collect-licenses.sh <dist> <deps-prefix> <libtorrent-prefix>
#   <dist>              the package folder (holds app/)
#   <deps-prefix>       where build-mpv.sh installed FFmpeg, libplacebo and mpv
#   <libtorrent-prefix> where third_party/libtorrent/build.sh installed libtorrent
set -euo pipefail

DIST=$1
DEPS=$2
TORRENT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
LIC="$DIST/licenses"
PREFIX=${MINGW_PREFIX:-/ucrt64}
PKG_PREFIX=${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}-
REPO_URL="${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-tomervered/corporal-jackie}"
COMMIT="${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD)}"

rm -rf "$LIC" && mkdir -p "$LIC"
cp "$ROOT/packaging/THIRD_PARTY_NOTICES.txt" "$DIST/THIRD_PARTY_NOTICES.txt"
# What the app sends and keeps, for the person who installs it.
cp "$ROOT/docs/PRIVACY.md" "$DIST/PRIVACY.md"

# --- Libraries we build: license files from their sources at the same tags.
version() { sed -n "s/^$1=//p" "$HERE/build-mpv.sh"; }
FFMPEG_VERSION=$(version FFMPEG_VERSION)
LIBPLACEBO_VERSION=$(version LIBPLACEBO_VERSION)
MPV_VERSION=$(version MPV_VERSION)
fetch() {  # fetch <folder> <github owner/repo> <tag> <file>...
    local dir="$LIC/$1" repo=$2 tag=$3
    shift 3
    mkdir -p "$dir"
    for f in "$@"; do
        local url="https://raw.githubusercontent.com/$repo/$tag/$f"
        curl -fsSL --retry 3 -o "$dir/$f" "$url" || { echo "Could not fetch $url" >&2; exit 1; }
    done
}
# libtorrent is linked into the app (static), patched by us.
LIBTORRENT_VERSION=v$(sed -n 's/^VERSION=//p' "$ROOT/third_party/libtorrent/build.sh")
mkdir -p "$LIC/libtorrent"
cp "$TORRENT/share/corporal-jackie/libtorrent-COPYING" "$LIC/libtorrent/COPYING"
cp "$TORRENT"/share/corporal-jackie/0*.patch "$LIC/libtorrent/"
fetch ffmpeg FFmpeg/FFmpeg "$FFMPEG_VERSION" LICENSE.md COPYING.GPLv3
fetch libplacebo haasn/libplacebo "$LIBPLACEBO_VERSION" LICENSE
fetch mpv mpv-player/mpv "$MPV_VERSION" Copyright LICENSE.GPL LICENSE.LGPL

# --- MSYS2 packages: whichever package owns each shipped DLL. Qt's plugins
# and QML modules come from qt6-base and qt6-declarative.
owned=$(
    for f in $(find "$DIST/app" -maxdepth 1 -iname '*.dll'); do
        b=$(basename "$f")
        [ -f "$DEPS/bin/$b" ] && continue          # one of ours, above
        [ -f "$PREFIX/bin/$b" ] && echo "$PREFIX/bin/$b"
    done | xargs -r pacman -Qqo 2>/dev/null || true
)
pkgs=$(printf '%s\n' $owned "${PKG_PREFIX}qt6-base" "${PKG_PREFIX}qt6-declarative" | sort -u)

missing=""
for p in $pkgs; do
    name=${p#"$PKG_PREFIX"}
    files=$(pacman -Qlq "$p" | grep -v '/$' \
        | grep -E '/share/licenses/|/share/doc/[^/]+/(COPYING|LICEN[CS]E|COPYRIGHT|NOTICE)[^/]*$' || true)
    if [ -z "$files" ]; then
        # A few packages ship no license file; packaging/licenses has them.
        fallback="$ROOT/packaging/licenses/$name"
        case $name in qt*) fallback="$ROOT/packaging/licenses/qt" ;; esac
        if [ -d "$fallback" ]; then
            mkdir -p "$LIC/$name" && cp "$fallback"/* "$LIC/$name/"
        else
            missing="$missing $name"
        fi
        continue
    fi
    mkdir -p "$LIC/$name"
    for f in $files; do cp "$f" "$LIC/$name/"; done
done

{
    echo "Where to get the source code of this package"
    echo
    echo "Corporal Jackie (GPL-3.0-or-later), the exact source of this build:"
    echo "  $REPO_URL/tree/$COMMIT"
    echo
    echo "Built from unmodified upstream source by $REPO_URL/blob/$COMMIT/packaging/windows/build-mpv.sh:"
    printf '  %-20s %s\n' "FFmpeg $FFMPEG_VERSION" "https://github.com/FFmpeg/FFmpeg/tree/$FFMPEG_VERSION" \
        "libplacebo $LIBPLACEBO_VERSION" "https://github.com/haasn/libplacebo/tree/$LIBPLACEBO_VERSION" \
        "mpv $MPV_VERSION" "https://github.com/mpv-player/mpv/tree/$MPV_VERSION"
    echo
    echo "libtorrent $LIBTORRENT_VERSION (BSD-3-Clause), linked into the app, with one patch of ours:"
    echo "  https://github.com/arvidn/libtorrent/tree/$LIBTORRENT_VERSION"
    echo "  $REPO_URL/tree/$COMMIT/third_party/libtorrent (build script and patch)"
    echo
    echo "Unmodified MSYS2 UCRT64 packages. Source archives for each version are at"
    echo "https://repo.msys2.org/mingw/sources/ and the build recipes at"
    echo "https://github.com/msys2/MINGW-packages."
    pacman -Q $pkgs | sed 's/^/  /'
    echo
    echo "If any of these links stops working, open an issue at $REPO_URL/issues"
    echo "and we will send you the source code for this build."
} > "$DIST/SOURCE.txt"

echo "License folders: $(ls "$LIC" | wc -l)"
ls "$LIC" | tr '\n' ' '; echo
if [ -n "$missing" ]; then
    echo "::error::No license files found in these MSYS2 packages:$missing"
    exit 1
fi
