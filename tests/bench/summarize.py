#!/usr/bin/env python3
"""Summarizes tst_bench results (CJ_BENCH_OUT, one JSON object per line).

    summarize.py results.jsonl [more.jsonl ...]

One row per link, scenario, storage mode and engine: the number of runs,
failed runs, and for each measure the median with the lowest and highest
value. Failed and timed-out runs are counted, never dropped. With few runs
the range is the honest summary; the median alone hides the spread.
"""
import json
import statistics
import sys
from collections import defaultdict

MiB = 1024 * 1024
MEASURES = [
    # key, label, scale
    ("firstByteMs", "first byte ms", 1),
    ("playStallMs", "stalled ms", 1),
    ("longestStallMs", "longest ms", 1),
    ("seekMissingMs", "seek new ms", 1),
    ("seekEvictedMs", "seek dropped ms", 1),
    ("cpuMs", "CPU ms", 1),
    ("fetchedBytes", "fetched MiB", 1 / MiB),
    ("peakProcessBytes", "peak MiB", 1 / MiB),
]


def seek_median(run, kind):
    times = sorted(s["ms"] for s in run.get("seeks", []) if s.get("kind") == kind)
    return times[len(times) // 2] if times else None


def main(paths):
    groups = defaultdict(list)
    for path in paths:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if not line.strip():
                    continue
                run = json.loads(line)
                run["seekMissingMs"] = seek_median(run, "missing")
                run["seekEvictedMs"] = seek_median(run, "evicted")
                key = (run.get("link", "loopback"), run.get("scenario", "?"),
                       "ram" if run.get("ramOnly") else "disk", run.get("engine", "?"))
                groups[key].append(run)

    print("| link | scenario | storage | engine | runs (failed) | "
          + " | ".join(label for _, label, _ in MEASURES) + " |")
    print("|" + "---|" * (5 + len(MEASURES)))
    for key in sorted(groups):
        runs = groups[key]
        ok = [r for r in runs if not r.get("failed")]
        cells = []
        for name, _, scale in MEASURES:
            values = [r[name] * scale for r in ok if isinstance(r.get(name), (int, float)) and r[name] >= 0]
            if not values:
                cells.append("-")
            elif len(values) == 1:
                cells.append(f"{values[0]:.0f}")
            else:
                cells.append(f"{statistics.median(values):.0f} ({min(values):.0f}-{max(values):.0f})")
        print(f"| {' | '.join(key)} | {len(runs)} ({len(runs) - len(ok)}) | " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main(sys.argv[1:])
