// Same magnet, seeder process and read workload as bench_player.cpp.
const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');
const { pathToFileURL } = require('node:url');
const MiB = 1048576;
const chunk = MiB / 4;
const [magnet, cache] = process.argv.slice(2);
const engineName = process.env.CJ_COMPETITOR || 'webtorrent';
const dependencies = path.resolve(process.env.CJ_COMPETITOR_MODULES);
const options = { path: cache, tmp: cache, dht: false, tracker: false, lsd: false, utp: false, natUpnp: false, natPmp: false };
const peers = new URL(magnet).searchParams.getAll('x.pe');
const sleep = ms => new Promise(resolve => setTimeout(resolve, Math.max(0, ms)));
const started = performance.now();
const timeout = setTimeout(() => { console.error('Competitor timed out'); process.exit(2); }, 180000);
let engine, client;

// CJ_COMPETITOR: "webtorrent" (current, an ES module), "webtorrent1" (1.9.7,
// the audit's pinned version, installed under that alias) or "torrent-stream"
// (1.2.1, the engine Stremio's streaming server grew from).
let WebTorrent;

// The same measure as bench_player: VmHWM on Linux. maxRSS would not do
// there, since Linux carries it across exec and it starts at the size of
// tst_bench, which forked this process.
function peakProcessBytes() {
    try {
        const hwm = /^VmHWM:\s+(\d+) kB/m.exec(fs.readFileSync('/proc/self/status', 'utf8'));
        if (hwm)
            return Number(hwm[1]) * 1024;
    } catch (e) {}
    return process.resourceUsage().maxRSS * 1024;
}

async function main() {
    fs.mkdirSync(cache, { recursive: true });
    if (engineName === 'webtorrent')
        WebTorrent = (await import(pathToFileURL(path.join(dependencies, 'webtorrent', 'index.js')).href)).default;
    else if (engineName === 'webtorrent1')
        WebTorrent = require(path.join(dependencies, 'webtorrent1'));
    await new Promise(resolve => {
        if (engineName.startsWith('webtorrent')) {
            client = new WebTorrent(options);
            engine = client.add(magnet, options, resolve);
            engine.once('infoHash', () => peers.forEach(peer => engine.addPeer(peer)));
        } else {
            engine = require(path.join(dependencies, 'torrent-stream'))(magnet, options);
            engine.listen(0, () => peers.forEach(peer => engine.connect(peer)));
            engine.once('ready', resolve);
        }
    });
    const result = { engine: engineName, ramOnly: false, openMs: Math.round(performance.now() - started) };
    const file = engine.files[0];
    async function read(at, length) {
        await new Promise((resolve, reject) => {
            let bytes = 0;
            file.createReadStream({ start: at, end: at + length - 1 })
                .on('data', data => { bytes += data.length; })
                .on('error', reject)
                .on('end', () => bytes === length ? resolve() : reject(new Error('Short read')));
        });
    }
    await read(0, 65536);
    result.firstByteMs = Math.round(performance.now() - started);
    const playStarted = performance.now();
    let stalls = 0, stallMs = 0, longest = 0;
    const played = Math.min(file.length / 2, 48 * MiB);
    // Playback reads one stream from the start, as a player reading over
    // HTTP does (Stremio's server hands torrent-stream's stream to the
    // player). A new stream per chunk would throw away each client's
    // read-ahead: torrent-stream then fetched only the chunk being read and
    // stalled about 2 s on a plain loopback seeder.
    const stream = file.createReadStream({ start: 0, end: played - 1 });
    const pull = stream[Symbol.asyncIterator]();
    let got = 0;
    for (let at = 0; at < played; at += chunk) {
        await sleep(at / (8 * MiB) * 1000 + playStarted - performance.now());
        const start = performance.now();
        while (got < Math.min(at + chunk, played)) {
            const { value, done } = await pull.next();
            if (done)
                throw new Error('Short read');
            got += value.length;
        }
        const elapsed = performance.now() - start;
        if (elapsed > 50) { stalls++; stallMs += elapsed; longest = Math.max(longest, elapsed); }
    }
    stream.destroy();
    Object.assign(result, { playedBytes: played, playStalls: stalls, playStallMs: Math.round(stallMs), longestStallMs: Math.round(longest) });
    result.seeks = [];
    for (const fraction of [.02, .45, .10, .55, .30, .90, .05, .60, .20, .75, .40, .95, .15, .65, .35, .85, .25, .70, .48, .80]) {
        const at = Math.floor(file.length * fraction / chunk) * chunk;
        const held = engine.bitfield.get(Math.floor(at / MiB));
        const start = performance.now();
        await read(at, chunk);
        result.seeks.push({ at, kind: held ? 'cached' : 'missing', ms: Math.round(performance.now() - start) });
        await sleep(200);
    }
    result.peakProcessBytes = peakProcessBytes();
    const cpu = process.cpuUsage();
    result.cpuMs = Math.round((cpu.user + cpu.system) / 1000);
    // Bytes received from peers: a WebTorrent torrent counts them itself,
    // torrent-stream in its swarm.
    result.fetchedBytes = engine.downloaded ?? engine.swarm?.downloaded ?? -1;
    result.totalMs = Math.round(performance.now() - started);
    console.log(JSON.stringify(result));
    clearTimeout(timeout);
    (client || engine).destroy(() => process.exit(0));
}
main().catch(error => { console.error(error); process.exit(1); });
