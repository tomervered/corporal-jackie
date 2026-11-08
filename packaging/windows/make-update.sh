#!/usr/bin/env bash
# Makes a small update package: only the files of this build that differ from
# the previous one, plus a script that applies them to an installed copy.
#
#   make-update.sh DIST PREVIOUS OUT
#
# DIST and PREVIOUS each hold build.txt and manifest.txt (one line per file:
# SHA-256, a tab, the path relative to DIST). OUT is created.
set -euo pipefail

dist=$1
prev=$2
out=$3
here=$(cd "$(dirname "$0")" && pwd)

rm -rf "$out"
mkdir -p "$out/_update/files"

tr -d '\r' < "$prev/build.txt" > "$out/_update/from-build.txt"
cp "$here/apply-update.ps1" "$out/_update/"
cp "$here/apply-update.cmd" "$here/README-update.txt" "$out/"

# New or changed files (manifest.txt itself is not listed in it).
awk -F'\t' 'NR == FNR { old[$2] = $1; next } old[$2] != $1 { print $2 }' \
    "$prev/manifest.txt" "$dist/manifest.txt" > "$out/_update/changed.txt"
echo "manifest.txt" >> "$out/_update/changed.txt"
# Files the previous build had and this one does not.
awk -F'\t' 'NR == FNR { cur[$2] = 1; next } !($2 in cur) { print $2 }' \
    "$dist/manifest.txt" "$prev/manifest.txt" > "$out/_update/removed.txt"

while IFS= read -r f; do
    mkdir -p "$out/_update/files/$(dirname "$f")"
    cp "$dist/$f" "$out/_update/files/$f"
done < "$out/_update/changed.txt"

echo "Update from $(cat "$out/_update/from-build.txt") to $(cat "$dist/build.txt"):"
echo "  $(wc -l < "$out/_update/changed.txt") files new or changed, $(wc -l < "$out/_update/removed.txt") removed"
echo "  $(du -sh "$out" | cut -f1) in all (the full build: $(du -sh "$dist" | cut -f1))"
sed 's/^/  + /' "$out/_update/changed.txt"
sed 's/^/  - /' "$out/_update/removed.txt"
