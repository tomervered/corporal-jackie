# Peer-departure experiment

The two patches build a candidate for PR #3 at `0c3318b`. Jackie sets a deadline on opening or jumping between nonconsecutive pieces and prioritizes the continuation when a read crosses a piece boundary. The libtorrent patch caps the ordinary request target at 24 blocks per peer during sequential downloads with at least two connected seeds. It enforces this at queue refill, so the remote handshake cannot raise the target. The ordinary libtorrent pipeline applies when only one seed is connected.

During sequential downloading, the patched picker checks the first wanted piece. If a piece within the next eight indices has already verified, a different peer with space in its queue can request blocks that have exactly one outstanding request. It skips blocks already queued on that connection, choked connections and peers on parole. Verification and cancellation use libtorrent's existing mechanisms. This is an experimental policy for the benchmark's four rate-limited seeders. At 16 KiB per block, 24 requests amount to 384 KiB per peer. Faster peers with long round trips may need more.

## Reproduce on Ubuntu 24.04

Start in a clean checkout of the PR head with the project's CI build dependencies installed, plus Git, Boost development headers and OpenSSL development headers. Use separate builds for the seeders and player. These commands run the same stock harness against each player.

```bash
set -eu
repo=$PWD
work=$(mktemp -d)
git worktree add --detach "$work/candidate" 0c3318b
git -C "$work/candidate" apply "$repo/tests/bench/peer-departure/jackie.patch"

git clone --depth 1 --branch v2.0.10 https://github.com/arvidn/libtorrent.git "$work/lt"
git -C "$work/lt" submodule update --init deps/try_signal
git -C "$work/lt" apply "$repo/tests/bench/peer-departure/libtorrent.patch"
cmake -S "$work/lt" -B "$work/lt-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$work/lt-install" -DBUILD_SHARED_LIBS=ON
cmake --build "$work/lt-build" --parallel 3
cmake --install "$work/lt-build"

cmake -S "$repo" -B "$work/stock" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/stock" --target tst_bench
cmake -S "$work/candidate" -B "$work/player" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$work/lt-install"
cmake --build "$work/player" --target bench_player

export CJ_BENCH_RUNS=5 CJ_BENCH_OUT="$work/results.jsonl"
rows=()
for i in 1 2 3 4 5; do rows+=("run:swarm disk $i"); done
"$work/stock/tests/tst_bench" "${rows[@]}"
CJ_BENCH_LABEL=candidate CJ_BENCH_PLAYER_PATH="$work/player/tests/bench_player" \
  "$work/stock/tests/tst_bench" "${rows[@]}"

npm install --prefix "$work/clients" --ignore-scripts --no-audit --no-fund \
  webtorrent@3.0.21 webtorrent1@npm:webtorrent@1.9.7
npm rebuild --prefix "$work/clients" node-datachannel
for engine in webtorrent webtorrent1; do
  CJ_COMPETITOR="$engine" CJ_COMPETITOR_MODULES="$work/clients/node_modules" \
    CJ_BENCH_EXTERNAL="$repo/tests/bench/competitor.cjs" \
    "$work/stock/tests/tst_bench" "${rows[@]}"
done
python3 tests/bench/summarize.py "$CJ_BENCH_OUT"
```

For the impaired link, repeat the four client invocations in a disposable Linux runner with the CI loopback qdisc (`tc qdisc add dev lo root netem delay 20ms loss 0.5%`) and `CJ_BENCH_LINK="40ms RTT, 0.5% loss"`. Remove the qdisc afterward. Keep cold cache directories, the seeder departure timer and the 8 MiB/s read clock identical across clients.

## Measurements

`windows.jsonl` contains the local Windows loopback runs. The PR and both WebTorrents used the stock engine or their published packages. The `q64`, `q64seek` and `cap-only-24` controls used the handshake fix without the gap repair. The `hedge` rows include the repair. `hedge24` and `hedge24-holdout` use the chosen scheduling settings before the cross-boundary read fix. The `patched` rows include the cross-boundary fix and a global cap. The `multiseed` rows use the final patches. The original-deadlines control keeps the PR's three deadline pieces. The tagged holdout was a separate set of runs after choosing the settings.

`ubuntu.jsonl` contains the Ubuntu 24.04 VM runs. `pr3` is the PR head, `candidate` is the initial global cap and gap repair, `patched` adds the cross-boundary read fix, and `multiseed` is the delivered version. Both WebTorrents ran their published Node packages. The seeders always used Ubuntu's stock libtorrent 2.0.10. Clients ran in groups on a shared host, with three disk trials per engine and link for the peer-departure comparison.

| Median blocked-read time, disk | PR #3 | Final patches | WebTorrent 3.0.21 | WebTorrent 1.9.7 |
|---|---:|---:|---:|---:|
| Loopback | 830 ms | 0 ms | 0 ms | 0 ms |
| 40 ms RTT, 0.5% loss | 2,524 ms | 82 ms | 497 ms | 450 ms |

The final patch's three lossy departure runs recorded 0, 82 and 981 ms of total stalled time. WebTorrent 3 ranged from 0 to 695 ms and WebTorrent 1 from 65 to 459 ms. Single-seeder playback on the lossy link recorded 0 ms in all three final runs. An earlier global cap stalled for 11,539 ms in its single-seeder trial.

On lossy peer departure, the patched client reported a median of 2,435 ms of CPU, a peak working set of 186 MiB and about 5.9 MiB of redundant or hash-failed payload. The PR baseline used 2,082 ms of CPU and peaked at 164 MiB. The benchmark counts playback reads that took over 50 ms. A player with a demuxer cache can show a different visible freeze time.

All 26 broker checks passed on Ubuntu with libtorrent 2.0.10 and on Windows with libtorrent 2.1.2 using the final patches.
