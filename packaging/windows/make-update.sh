#!/usr/bin/env bash
# Makes a small update package: only the files of this build that differ from
# the previous one, with this build's signed list of every file.
#
#   make-update.sh DIST PREVIOUS OUT
#
# DIST and PREVIOUS each hold build.txt and manifest.txt (one line per file:
# SHA-256, a tab, the path relative to DIST); DIST also holds manifest.sig.
# OUT is created with:
#   files/        the new and changed files, at their paths in the install
#   build.txt     this build
#   manifest.txt  every file of this build, with its hash
#   manifest.sig  the signature over build.txt + manifest.txt
#   README-update.txt
# The package carries no script. The installed copy's own updater
# (app/updater/apply-update.ps1) checks and applies it, and works out which
# files to remove from the two manifests itself.
set -euo pipefail

dist=$1
prev=$2
out=$3
here=$(cd "$(dirname "$0")" && pwd)

rm -rf "$out"
mkdir -p "$out/files"
cp "$dist/build.txt" "$dist/manifest.txt" "$out/"
[ -f "$dist/manifest.sig" ] && cp "$dist/manifest.sig" "$out/"
cp "$here/README-update.txt" "$out/"

# New or changed files.
awk -F'\t' 'NR == FNR { old[$2] = $1; next } !($2 in old) || old[$2] != $1 { print $2 }' \
    "$prev/manifest.txt" "$dist/manifest.txt" > "$out/changed.txt"
removed=$(awk -F'\t' 'NR == FNR { cur[$2] = 1; next } !($2 in cur) { print $2 }' "$dist/manifest.txt" "$prev/manifest.txt")

while IFS= read -r f; do
    mkdir -p "$out/files/$(dirname "$f")"
    cp "$dist/$f" "$out/files/$f"
done < "$out/changed.txt"

echo "Update from $(tr -d '\r' < "$prev/build.txt") to $(cat "$dist/build.txt"):"
echo "  $(wc -l < "$out/changed.txt") files new or changed, $(printf '%s' "$removed" | grep -c . || true) removed"
echo "  $(du -sh "$out" | cut -f1) in all (the full build: $(du -sh "$dist" | cut -f1))"
[ -f "$out/manifest.sig" ] || echo "  NOT SIGNED: the updater will refuse it"
sed 's/^/  + /' "$out/changed.txt"
printf '%s\n' "$removed" | sed '/^$/d; s/^/  - /'
rm "$out/changed.txt"
