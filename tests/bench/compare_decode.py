#!/usr/bin/env python3
"""Compare two bench_decode results: the old libraries against the new ones.

    compare_decode.py old.json new.json [--tolerance 0.10]

Prints a table and exits 1 when the new build is slower or heavier than the
old one by more than the tolerance (default 10 %): lower decode_fps, higher
cpu_seconds, peak_bytes, seek_ms_median. One run on a shared CI machine is
noisy; pass several results per side with `old.json,old2.json` and the median
is used.
"""
import json, statistics, sys

def load(arg):
    runs = [json.load(open(p)) for p in arg.split(",")]
    return {k: statistics.median(r[k] for r in runs) for k in runs[0] if k != "libs"}, runs[0]["libs"]

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    tol = 0.10
    if "--tolerance" in sys.argv:
        tol = float(sys.argv[sys.argv.index("--tolerance") + 1])
        args = [a for a in args if a != str(tol) and a != sys.argv[sys.argv.index("--tolerance") + 1]]
    old, oldlibs = load(args[0])
    new, newlibs = load(args[1])
    higher_is_better = {"decode_fps"}
    watched = ["decode_fps", "cpu_seconds", "peak_bytes", "seek_ms_median"]
    print(f"{'metric':<16}{'old':>14}{'new':>14}{'change':>10}")
    bad = []
    for k in watched:
        o, n = old[k], new[k]
        change = (n - o) / o if o else 0.0
        worse = -change if k in higher_is_better else change
        flag = "  WORSE" if worse > tol else ""
        if worse > tol:
            bad.append(k)
        print(f"{k:<16}{o:>14.2f}{n:>14.2f}{change:>+9.1%}{flag}")
    print(f"\nold libs: {oldlibs['mpv']} / FFmpeg {oldlibs['ffmpeg']}")
    print(f"new libs: {newlibs['mpv']} / FFmpeg {newlibs['ffmpeg']}")
    if bad:
        print(f"\nSlower or heavier than {tol:.0%}: {', '.join(bad)}")
        return 1
    return 0

sys.exit(main())
