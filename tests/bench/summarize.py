#!/usr/bin/env python3
"""Summarizes tst_bench results (CJ_BENCH_OUT, one JSON object per line).

    summarize.py results.jsonl [more.jsonl ...]

Two tables (start and playback; seeks and cost), one row per link,
scenario, storage mode and engine: the number of runs, failed runs, and for
each measure the median with the lowest and highest value. "seek ms (all
20)" is the median over every seek, for clients that cannot say which
pieces they hold. Failed and timed-out runs are counted, never dropped. With few runs
the range is the honest summary; the median alone hides the spread.
"""
import json
import statistics
import sys
from collections import defaultdict

MiB = 1024 * 1024
TABLES = [
    ("Start and playback", [
        # key, label, scale
        ("openMs", "open ms", 1),
        ("firstByteMs", "first byte ms", 1),
        ("playStallMs", "stalled ms", 1),
        ("playStalls", "stalls", 1),
        ("longestStallMs", "longest ms", 1),
    ]),
    ("Seeks and cost", [
        ("seekAllMs", "seek ms (all 20)", 1),
        ("seekMissingMs", "seek new ms", 1),
        ("seekEvictedMs", "seek dropped ms", 1),
        ("cpuMs", "CPU ms", 1),
        ("fetchedBytes", "fetched MiB", 1 / MiB),
        ("peakProcessBytes", "peak MiB", 1 / MiB),
    ]),
]


def seek_median(run, kind=None):
    times = sorted(s["ms"] for s in run.get("seeks", []) if kind is None or s.get("kind") == kind)
    return times[len(times) // 2] if times else None


def cell(values):
    if not values:
        return "-"
    if len(values) == 1:
        return f"{values[0]:.0f}"
    return f"{statistics.median(values):.0f} ({min(values):.0f}-{max(values):.0f})"


def main(paths):
    groups = defaultdict(list)
    for path in paths:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if not line.strip():
                    continue
                run = json.loads(line)
                run["seekAllMs"] = seek_median(run)
                run["seekMissingMs"] = seek_median(run, "missing")
                run["seekEvictedMs"] = seek_median(run, "evicted")
                key = (run.get("link", "loopback"), run.get("scenario", "?"),
                       "ram" if run.get("ramOnly") else "disk", run.get("engine", "?"))
                groups[key].append(run)

    for title, measures in TABLES:
        print(f"### {title}\n")
        print("| link | scenario | storage | engine | runs (failed) | "
              + " | ".join(label for _, label, _ in measures) + " |")
        print("|" + "---|" * (5 + len(measures)))
        for key in sorted(groups):
            runs = groups[key]
            ok = [r for r in runs if not r.get("failed")]
            cells = [cell([r[name] * scale for r in ok if isinstance(r.get(name), (int, float)) and r[name] >= 0])
                     for name, _, scale in measures]
            print(f"| {' | '.join(key)} | {len(runs)} ({len(runs) - len(ok)}) | " + " | ".join(cells) + " |")
        print()


if __name__ == "__main__":
    main(sys.argv[1:])
