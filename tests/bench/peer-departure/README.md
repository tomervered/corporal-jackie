# Peer-departure measurements

How playback behaves when one of several seeders leaves, and the data behind the choices in `third_party/libtorrent/0001-sequential-peer-departure.patch` and `StreamHandle::read` / `Engine::schedule`. The design is in [docs/ENGINE.md](../../../docs/ENGINE.md). The two patches were first developed as a stand-alone experiment against PR #3's head (`0c3318b`); both are now in the tree.

## Reproduce on Ubuntu 24.04

Install the project's CI dependencies, plus Boost headers and OpenSSL development files. The player under test is this tree, built against the patched libtorrent. The baseline is PR #3's head with stock libtorrent.

```bash
set -eu
repo=$PWD
work=$(mktemp -d)

# This tree, against the patched libtorrent.
third_party/libtorrent/build.sh "$work/lt"
cmake -S "$repo" -B "$work/new" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$work/lt"
cmake --build "$work/new" --target tst_bench bench_player

# PR #3's head with the distribution's libtorrent.
git worktree add --detach "$work/pr3" 0c3318b
cmake -S "$work/pr3" -B "$work/pr3/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/pr3/build" --target bench_player

export CJ_BENCH_RUNS=5 CJ_BENCH_OUT="$work/results.jsonl"
rows=()
for i in 1 2 3 4 5; do rows+=("run:swarm disk $i"); done
"$work/new/tests/tst_bench" "${rows[@]}"
CJ_BENCH_LABEL=pr3 CJ_BENCH_PLAYER_PATH="$work/pr3/build/tests/bench_player" \
  "$work/new/tests/tst_bench" "${rows[@]}"

npm install --prefix "$work/clients" --ignore-scripts --no-audit --no-fund \
  webtorrent@3.0.21 webtorrent1@npm:webtorrent@1.9.7
npm rebuild --prefix "$work/clients" node-datachannel
for engine in webtorrent webtorrent1; do
  CJ_COMPETITOR="$engine" CJ_COMPETITOR_MODULES="$work/clients/node_modules" \
    CJ_BENCH_EXTERNAL="$repo/tests/bench/competitor.cjs" \
    "$work/new/tests/tst_bench" "${rows[@]}"
done
python3 tests/bench/summarize.py "$CJ_BENCH_OUT"
```

For the impaired link, repeat the four client invocations in a disposable Linux runner with the CI loopback qdisc (`tc qdisc add dev lo root netem delay 20ms loss 0.5%`) and `CJ_BENCH_LINK="40ms RTT, 0.5% loss"`. Remove the qdisc afterward. Keep cold cache directories, the seeder departure timer and the 8 MiB/s read clock identical across clients.

## Final build

`final-linux.jsonl`: this tree (`new`: patched libtorrent 2.1.2, bundled) against PR #3's head (`pr3`: distribution libtorrent 2.0.10) and WebTorrent 1.9.7, six runs each on one Linux machine in shuffled order, loopback, cold caches. Seeders are the benchmark's libtorrent sessions (4 at 3 MiB/s, one leaves at 4 s; the single-seeder rows use one fast seeder). Medians, ranges in `summarize.py`'s output:

| Scenario, storage | Engine | Stalled ms | Stalls | Longest ms | First byte ms | CPU ms | Peak MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| departure, disk | new | 0 | 0 | 0 | 1115 | 522 | 93 |
| departure, disk | pr3 | 1080 | 4 | 376 | 1122 | 750 | 165 |
| departure, disk | WebTorrent 1.9.7 | 0 (max 51) | 0 | 0 | 1156 | 1706 | 131 |
| departure, RAM | new | 0 | 0 | 0 | 1118 | 522 | 79 |
| departure, RAM | pr3 | 952 | 6 | 276 | 1120 | 548 | 78 |
| single seeder, disk | new | 0 | 0 | 0 | 54 | 462 | 94 |
| single seeder, disk | pr3 | 0 | 0 | 0 | 79 | 595 | 171 |
| single seeder, disk | WebTorrent 1.9.7 | 0 | 0 | 0 | 166 | 1501 | 128 |
| single seeder, RAM | new | 0 | 0 | 0 | 63 | 476 | 75 |
| single seeder, RAM | pr3 | 0 | 0 | 0 | 70 | 478 | 74 |

Seeks to data not yet downloaded (departure, disk): new 302 ms, pr3 406 ms, WebTorrent 1.9.7 970 ms. In RAM mode: new 300 ms, pr3 598 ms. The lower memory and CPU of `new` on disk come mostly from libtorrent 2.1's file I/O (reads, not memory maps), not from the patch. The impaired-link rows below are from the earlier experiment; they could not be repeated here, because the container has no `tc`.

## Earlier measurements (experiment, before the patches were merged)

`windows.jsonl` contains the local Windows loopback runs. The PR and both WebTorrents used the stock engine or their published packages. The `q64`, `q64seek` and `cap-only-24` controls used the handshake fix without the gap repair. The `hedge` rows include the repair. `hedge24` and `hedge24-holdout` use the chosen scheduling settings before the cross-boundary read fix. The `patched` rows include the cross-boundary fix and a global cap. The `multiseed` rows use the final patches. The original-deadlines control keeps the PR's three deadline pieces. The tagged holdout was a separate set of runs after choosing the settings.

`ubuntu.jsonl` contains the Ubuntu 24.04 VM runs. `pr3` is the PR head, `candidate` is the initial global cap and gap repair, `patched` adds the cross-boundary read fix, and `multiseed` is the delivered version. Both WebTorrents ran their published Node packages. The seeders always used Ubuntu's stock libtorrent 2.0.10. Clients ran in groups on a shared host, with three disk trials per engine and link for the peer-departure comparison.

| Median blocked-read time, disk | PR #3 | Final patches | WebTorrent 3.0.21 | WebTorrent 1.9.7 |
|---|---:|---:|---:|---:|
| Loopback | 830 ms | 0 ms | 0 ms | 0 ms |
| 40 ms RTT, 0.5% loss | 2,524 ms | 82 ms | 497 ms | 450 ms |

The final patch's three lossy departure runs recorded 0, 82 and 981 ms of total stalled time. WebTorrent 3 ranged from 0 to 695 ms and WebTorrent 1 from 65 to 459 ms. Single-seeder playback on the lossy link recorded 0 ms in all three final runs. An earlier global cap stalled for 11,539 ms in its single-seeder trial.

On lossy peer departure, the patched client reported a median of 2,435 ms of CPU, a peak working set of 186 MiB and about 5.9 MiB of redundant or hash-failed payload. The PR baseline used 2,082 ms of CPU and peaked at 164 MiB. The benchmark counts playback reads that took over 50 ms. A player with a demuxer cache can show a different visible freeze time.

All 26 broker checks passed on Ubuntu with libtorrent 2.0.10 and on Windows with libtorrent 2.1.2 using the final patches.
