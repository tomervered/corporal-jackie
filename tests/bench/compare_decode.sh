#!/usr/bin/env bash
# Compares two folders written by run_decode.sh, clip by clip (the median of
# three runs on each side). Exits 1 if any clip got slower or heavier than the
# tolerance (default 0.15; shared CI machines are noisy).
#
# Usage: compare_decode.sh <old dir> <new dir> [tolerance]
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OLD=$1
NEW=$2
TOL=${3:-0.15}
status=0
for first in "$NEW"/*-1.json; do
    name=$(basename "$first" -1.json)
    [ -f "$OLD/$name-1.json" ] || { echo "$name: no old result"; continue; }
    echo "== $name"
    python3 "$HERE/compare_decode.py" \
        "$OLD/$name-1.json,$OLD/$name-2.json,$OLD/$name-3.json" \
        "$NEW/$name-1.json,$NEW/$name-2.json,$NEW/$name-3.json" --tolerance "$TOL" || status=1
done
exit $status
