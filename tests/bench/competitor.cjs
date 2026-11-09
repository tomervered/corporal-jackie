// Same magnet, seeder process and read workload as bench_player.cpp.
const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');
const MiB = 1048576;
const chunk = MiB / 4;
const [magnet, cache] = process.argv.slice(2);
const engineName = process.env.CJ_COMPETITOR;
const dependencies = path.resolve(process.env.CJ_COMPETITOR_MODULES);
const options = { path: cache, tmp: cache, dht: false, tracker: false, lsd: false, utp: false, natUpnp: false, natPmp: false };
const peers = new URL(magnet).searchParams.getAll('x.pe');
const sleep = ms => new Promise(resolve => setTimeout(resolve, Math.max(0, ms)));
const started = performance.now();
const timeout = setTimeout(() => { console.error('Competitor timed out'); process.exit(2); }, 180000);
let engine, client;

async function main() {
    fs.mkdirSync(cache, { recursive: true });
    await new Promise(resolve => {
        if (engineName === 'webtorrent') {
            const WebTorrent = require(path.join(dependencies, 'webtorrent'));
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
    for (let at = 0; at < played; at += chunk) {
        await sleep(at / (8 * MiB) * 1000 + playStarted - performance.now());
        const start = performance.now();
        await read(at, chunk);
        const elapsed = performance.now() - start;
        if (elapsed > 50) { stalls++; stallMs += elapsed; longest = Math.max(longest, elapsed); }
    }
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
    result.peakProcessBytes = process.resourceUsage().maxRSS * 1024;
    result.totalMs = Math.round(performance.now() - started);
    console.log(JSON.stringify(result));
    clearTimeout(timeout);
    (client || engine).destroy(() => process.exit(0));
}
main().catch(error => { console.error(error); process.exit(1); });
