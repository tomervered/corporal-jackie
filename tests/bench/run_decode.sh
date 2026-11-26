#!/usr/bin/env bash
# Runs bench_decode on every clip in tests/data/bench, three times each, and
# writes <outdir>/<clip>-<n>.json. Compare two such folders with
# compare_decode.sh.
#
# Usage: run_decode.sh <bench_decode> <outdir>
set -euo pipefail
BIN=$1
OUT=$2
HERE=$(cd "$(dirname "$0")" && pwd)
# With two library folders, alternate order for each clip on this machine.
if [[ $# -eq 4 ]]; then
    mkdir -p "$OUT/main" "$OUT/new"
    for clip in "$HERE"/../data/bench/*.mkv; do
        name=$(basename "$clip" .mkv)
        for n in 1 2 3; do
            order=(main new)
            if [[ $n == 2 ]]; then order=(new main); fi
            for build in "${order[@]}"; do
                libs=$3
                if [[ $build == new ]]; then libs=$4; fi
                PATH="$libs:$PATH" "$BIN" "$clip" 10 30 > "$OUT/$build/$name-$n.json"
            done
        done
    done
    exit 0
fi
mkdir -p "$OUT"
for clip in "$HERE"/../data/bench/*.mkv; do
    name=$(basename "$clip" .mkv)
    for n in 1 2 3; do
        "$BIN" "$clip" 10 30 > "$OUT/$name-$n.json"
    done
    echo "$name: $(python3 -c "import json,sys;r=[json.load(open(f'$OUT/$name-{n}.json')) for n in (1,2,3)];print('%.0f fps, %.0f ms seek' % (sorted(x['decode_fps'] for x in r)[1], sorted(x['seek_ms_median'] for x in r)[1]))")"
done
