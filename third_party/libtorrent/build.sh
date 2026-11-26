#!/usr/bin/env bash
# Builds libtorrent-rasterbar from the pinned release, with our patch, as a
# static library installed into <prefix>. Works the same on Linux, macOS and
# MSYS2 UCRT64 (Windows). Needs cmake, ninja, a C++ compiler, curl, tar,
# Boost headers and OpenSSL development files.
#
# Usage: build.sh <prefix> [source-tarball]   (skips if already built)
#
# Point the app's build at it with -DCMAKE_PREFIX_PATH=<prefix>. The patch
# (0001-sequential-peer-departure.patch) is what keeps playback from stalling
# when a seeder leaves; see docs/ENGINE.md.
set -euo pipefail

PREFIX=$1
HERE=$(cd "$(dirname "$0")" && pwd)

# The release tarball includes the bundled dependencies (unlike a git tag).
VERSION=2.1.2
SHA256=3362546d9cd71b9e49ee6cac7d3f1f914ce9cdb217c86b63d5b22cbed0334dbc
URL="https://github.com/arvidn/libtorrent/releases/download/v$VERSION/libtorrent-rasterbar-$VERSION.tar.gz"
PATCHES=$(cd "$HERE" && ls 0*.patch)
PATCH_HASH=$(cat "$HERE"/0*.patch | (sha256sum 2>/dev/null || shasum -a 256) | cut -c1-12)
STAMP="$PREFIX/share/corporal-jackie/libtorrent-$VERSION-$PATCH_HASH"

if [ -f "$STAMP" ]; then
    echo "Using the libtorrent build in $PREFIX"
    exit 0
fi

SRC=$(mktemp -d)
trap 'rm -rf "$SRC"' EXIT
TARBALL=${2:-$SRC/libtorrent.tar.gz}
[ -f "$TARBALL" ] || curl -fsSL --retry 3 -o "$TARBALL" "$URL"
sum=$( (sha256sum "$TARBALL" 2>/dev/null || shasum -a 256 "$TARBALL") | cut -d' ' -f1)
if [ "$sum" != "$SHA256" ]; then
    echo "libtorrent tarball checksum mismatch: $sum" >&2
    exit 1
fi
tar -xzf "$TARBALL" -C "$SRC"
cd "$SRC/libtorrent-rasterbar-$VERSION"
for p in $PATCHES; do
    git apply --ignore-whitespace "$HERE/$p"
done

# MSYS2's native cmake wants Windows-style paths.
CMAKE_PREFIX=$PREFIX
command -v cygpath >/dev/null && CMAKE_PREFIX=$(cygpath -m "$PREFIX")
EXTRA=()
if [ "$(uname -s)" = Darwin ] && command -v brew >/dev/null; then
    EXTRA+=(-DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)")
fi
# Windows: Boost 1.92's IOCP timer thread has crashed on freed memory when a
# session ends (tests/diag/session_churn_probe.cpp reproduces it in seconds).
# Use Asio's select-based reactor there instead; FD_SETSIZE lifts its default
# limit of 64 sockets.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EXTRA+=("-DCMAKE_CXX_FLAGS=-DBOOST_ASIO_DISABLE_IOCP -DFD_SETSIZE=1024") ;;
esac
rm -rf "$PREFIX/include/libtorrent" "$PREFIX"/lib/libtorrent-rasterbar* "$PREFIX"/lib/cmake/LibtorrentRasterbar
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$CMAKE_PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -Dwebtorrent=OFF \
    -Dbuild_tests=OFF -Dbuild_examples=OFF -Dbuild_tools=OFF -Dpython-bindings=OFF \
    "${EXTRA[@]}"
cmake --build build
cmake --install build

# The license text goes into the Windows and macOS packages; the stamp tells
# the app's build that this is the patched build.
mkdir -p "$PREFIX/share/corporal-jackie"
cp COPYING "$PREFIX/share/corporal-jackie/libtorrent-COPYING"
cp "$HERE"/0*.patch "$PREFIX/share/corporal-jackie/"
echo "libtorrent $VERSION with $PATCHES" > "$STAMP"
echo "Built libtorrent $VERSION in $PREFIX"
