#!/usr/bin/env bash
# Adds the third-party notices, every bundled library's license files and
# SOURCE.txt to the macOS package. Runs after macdeployqt.
#
# Usage: collect-licenses.sh <dist> <libtorrent-prefix>
#   <dist>              the folder that holds CorporalJackie.app
#   <libtorrent-prefix> where third_party/libtorrent/build.sh installed libtorrent
set -euo pipefail

DIST=$1
TORRENT=$2
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
LIC="$DIST/licenses"
BREW=$(brew --prefix)
REPO_URL="${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-tomervered/corporal-jackie}"
COMMIT="${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD)}"

rm -rf "$LIC" && mkdir -p "$LIC"
cp "$ROOT/packaging/THIRD_PARTY_NOTICES.txt" "$DIST/THIRD_PARTY_NOTICES.txt"

# Which Homebrew formula each bundled dylib or framework came from:
# Cellar/<formula>/<version>/lib/<name>.
index=$(find "$BREW/Cellar" -mindepth 4 -maxdepth 4 -path '*/lib/*' 2>/dev/null)
formulae=$(
    ls "$DIST/CorporalJackie.app/Contents/Frameworks" | while read -r name; do
        printf '%s\n' "$index" | grep -F "/lib/$name" | head -1 | sed -E "s|^$BREW/Cellar/([^/]+)/.*|\1|" || true
    done | sort -u
)

# libtorrent is linked into the app (static), patched by us.
LIBTORRENT_VERSION=v$(sed -n 's/^VERSION=//p' "$ROOT/third_party/libtorrent/build.sh")
mkdir -p "$LIC/libtorrent"
cp "$TORRENT/share/corporal-jackie/libtorrent-COPYING" "$LIC/libtorrent/COPYING"
cp "$TORRENT"/share/corporal-jackie/0*.patch "$LIC/libtorrent/"

missing=""
for f in $formulae; do
    keg="$BREW/opt/$f"
    found=$(find -L "$keg" -maxdepth 1 -type f \( -iname 'LICEN[CS]E*' -o -iname 'COPYING*' \
        -o -iname 'COPYRIGHT*' -o -iname 'NOTICE*' -o -iname 'Copyright' \) 2>/dev/null || true)
    if [ -d "$keg/share/licenses" ]; then
        found="$found $(find -L "$keg/share/licenses" -type f)"
    fi
    if [ -z "${found//[[:space:]]/}" ]; then
        # A few packages ship no license file; packaging/licenses has them.
        fallback="$ROOT/packaging/licenses/$f"
        case $f in qt*) fallback="$ROOT/packaging/licenses/qt" ;; esac
        if [ -d "$fallback" ]; then
            mkdir -p "$LIC/$f" && cp "$fallback"/* "$LIC/$f/"
        else
            missing="$missing $f"
        fi
        continue
    fi
    mkdir -p "$LIC/$f"
    for file in $found; do cp "$file" "$LIC/$f/"; done
done

{
    echo "Where to get the source code of this package"
    echo
    echo "Corporal Jackie (GPL-3.0-or-later), the exact source of this build:"
    echo "  $REPO_URL/tree/$COMMIT"
    echo
    echo "libtorrent $LIBTORRENT_VERSION (BSD-3-Clause), linked into the app, with one patch of ours:"
    echo "  https://github.com/arvidn/libtorrent/tree/$LIBTORRENT_VERSION"
    echo "  $REPO_URL/tree/$COMMIT/third_party/libtorrent (build script and patch)"
    echo
    echo "Unmodified Homebrew packages. Each formula page links the upstream"
    echo "source archive for its version: https://formulae.brew.sh/formula/<name>"
    for f in $formulae; do
        echo "  $(brew list --versions "$f")"
    done
    echo
    echo "If any of these links stops working, open an issue at $REPO_URL/issues"
    echo "and we will send you the source code for this build."
} > "$DIST/SOURCE.txt"

echo "License folders: $(ls "$LIC" | wc -l)"
ls "$LIC" | tr '\n' ' '; echo
if [ -n "$missing" ]; then
    echo "::error::No license files found for these formulae:$missing"
    exit 1
fi
