// Same magnet, seeders and read workload as bench_player.cpp, for another
// torrent client. tst_bench runs it as: node competitor.cjs <magnet> <cache>
//
// CJ_COMPETITOR picks the client:
//   webtorrent      WebTorrent 3 (current), in this process
//   webtorrent1     WebTorrent 1.9.7, the audit's pinned version (npm alias)
//   torrent-stream  torrent-stream 1.2.1, the engine peerflix is built on
//   qbittorrent     qbittorrent-nox (libtorrent), through its Web API, with
//                   sequential download and first and last pieces first
//   stremio         Stremio's own streaming server (CJ_STREMIO_IMAGE, a
//                   stremio/server Docker image), over its HTTP API
// CJ_COMPETITOR_MODULES is the node_modules folder holding the npm clients.
//
// The clock starts when the torrent is added, with the client already
// running, as bench_player starts it after creating its broker: loading
// modules or starting a daemon is not part of the first byte. CPU time and
// peak memory are for the process that does the torrent work (this one, or
// the daemon), over its whole life.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const net = require('node:net');
const { execFileSync, spawn } = require('node:child_process');
const { performance } = require('node:perf_hooks');
const { pathToFileURL } = require('node:url');
const MiB = 1048576;
const chunk = MiB / 4;
const [magnet, cache] = process.argv.slice(2);
const engineName = process.env.CJ_COMPETITOR || 'webtorrent';
const dependencies = path.resolve(process.env.CJ_COMPETITOR_MODULES || 'node_modules');
const options = { path: cache, tmp: cache, dht: false, tracker: false, lsd: false, utp: false, natUpnp: false, natPmp: false };
const peers = new URL(magnet).searchParams.getAll('x.pe');
const infoHash = /urn:btih:([0-9a-fA-F]{40})/.exec(magnet)[1].toLowerCase();
const sleep = ms => new Promise(resolve => setTimeout(resolve, Math.max(0, ms)));
let started = performance.now();
const cleanups = [];
function cleanUp() {
    while (cleanups.length) {
        try { cleanups.pop()(); } catch (e) {}
    }
}
function fail(message, code) {
    console.error(message);
    cleanUp();
    process.exit(code);
}
const timeout = setTimeout(() => fail('Competitor timed out', 2), 220000);

// Peak memory as bench_player measures it: VmHWM on Linux. maxRSS would not
// do for this process, since Linux carries it across exec and it starts at
// the size of tst_bench, which forked this process.
function peakBytes(pid) {
    try {
        const hwm = /^VmHWM:\s+(\d+) kB/m.exec(fs.readFileSync(`/proc/${pid}/status`, 'utf8'));
        if (hwm)
            return Number(hwm[1]) * 1024;
    } catch (e) {}
    return pid === 'self' ? process.resourceUsage().maxRSS * 1024 : -1;
}
// CPU time of another process, from /proc (user + system, in clock ticks).
function cpuMsOf(pid) {
    try {
        const fields = fs.readFileSync(`/proc/${pid}/stat`, 'utf8').split(') ')[1].split(' ');
        const ticks = Number(fields[11]) + Number(fields[12]);
        return Math.round(ticks * 1000 / Number(execFileSync('getconf', ['CLK_TCK']).toString()));
    } catch (e) {
        return -1;
    }
}
function selfUsage() {
    const cpu = process.cpuUsage();
    return { cpuMs: Math.round((cpu.user + cpu.system) / 1000), peakProcessBytes: peakBytes('self') };
}

function freePort() {
    return new Promise((resolve, reject) => {
        const server = net.createServer();
        server.listen(0, '127.0.0.1', () => {
            const { port } = server.address();
            server.close(() => resolve(port));
        });
        server.on('error', reject);
    });
}
function request(url, { method = 'GET', body, headers = {} } = {}) {
    return new Promise((resolve, reject) => {
        const req = http.request(url, { method, headers }, res => {
            const parts = [];
            res.on('data', data => parts.push(data));
            res.on('end', () => resolve({ status: res.statusCode, body: Buffer.concat(parts) }));
            res.on('error', reject);
        });
        req.on('error', reject);
        req.end(body);
    });
}
async function json(url, init) {
    const answer = await request(url, init);
    if (answer.status !== 200)
        throw new Error(`${url}: HTTP ${answer.status} ${answer.body.toString().slice(0, 200)}`);
    return JSON.parse(answer.body.toString());
}
function form(fields) {
    const body = new URLSearchParams(fields).toString();
    return { method: 'POST', body, headers: { 'content-type': 'application/x-www-form-urlencoded' } };
}
async function until(what, test, limitMs = 60000, everyMs = 50) {
    const end = performance.now() + limitMs;
    for (;;) {
        const value = await test().catch(() => undefined);
        if (value)
            return value;
        if (performance.now() > end)
            throw new Error(`Gave up waiting for ${what}`);
        await sleep(everyMs);
    }
}
// A byte range of an HTTP resource, as a stream.
function httpRange(url, start, end) {
    return new Promise((resolve, reject) => {
        http.get(url, { headers: { range: `bytes=${start}-${end}` } }, res => {
            if (res.statusCode !== 206 && res.statusCode !== 200) {
                res.resume();
                reject(new Error(`${url}: HTTP ${res.statusCode}`));
                return;
            }
            resolve(res);
        }).on('error', reject);
    });
}

// npm clients in this process.
async function openLibrary() {
    let WebTorrent, client, torrent;
    if (engineName === 'webtorrent')
        WebTorrent = (await import(pathToFileURL(path.join(dependencies, 'webtorrent', 'index.js')).href)).default;
    else if (engineName === 'webtorrent1')
        WebTorrent = require(path.join(dependencies, 'webtorrent1'));
    const torrentStream = WebTorrent ? null : require(path.join(dependencies, 'torrent-stream'));
    if (WebTorrent)
        client = new WebTorrent(options);
    started = performance.now();
    await new Promise(resolve => {
        if (client) {
            torrent = client.add(magnet, options, resolve);
            torrent.once('infoHash', () => peers.forEach(peer => torrent.addPeer(peer)));
        } else {
            torrent = torrentStream(magnet, options);
            torrent.listen(0, () => peers.forEach(peer => torrent.connect(peer)));
            torrent.once('ready', resolve);
        }
    });
    const file = torrent.files[0];
    return {
        length: file.length,
        stream: (start, end) => file.createReadStream({ start, end }),
        held: at => !!torrent.bitfield.get(Math.floor(at / torrent.pieceLength)),
        // A WebTorrent torrent counts bytes from peers itself, torrent-stream in its swarm.
        fetchedBytes: async () => torrent.downloaded ?? torrent.swarm?.downloaded ?? -1,
        usage: async () => selfUsage(),
        close: () => new Promise(resolve => (client || torrent).destroy(resolve)),
    };
}

// qbittorrent-nox, a fresh profile per run. It cannot stream, so a read
// waits until the pieces it covers are downloaded (pieceStates), then reads
// the file it is writing.
async function openQBittorrent() {
    const profile = path.join(cache, 'qbt-profile');
    const savePath = path.join(cache, 'qbt-download');
    const configDir = path.join(profile, 'qBittorrent', 'config');
    fs.mkdirSync(configDir, { recursive: true });
    fs.mkdirSync(savePath, { recursive: true });
    const webPort = await freePort();
    const listenPort = await freePort();
    // The same switches as the other clients: no DHT, LSD, PEX, uTP,
    // UPnP or trackers; only the seeders.
    fs.writeFileSync(path.join(configDir, 'qBittorrent.conf'), [
        '[LegalNotice]', 'Accepted=true', '',
        '[BitTorrent]',
        'Session\\DHTEnabled=false', 'Session\\LSDEnabled=false', 'Session\\PeXEnabled=false',
        'Session\\BTProtocol=TCP', 'Session\\QueueingSystemEnabled=false', 'Session\\AddTrackersEnabled=false',
        `Session\\DefaultSavePath=${savePath}`, 'Session\\TempPathEnabled=false', `Session\\Port=${listenPort}`,
        // Its piece states refresh every 1.5 s by default, which would add
        // up to 1.5 s to every read here; 50 ms costs it some CPU instead.
        'Session\\RefreshInterval=50',
        '', '[Network]', 'PortForwardingEnabled=false', '',
        '[Preferences]', `WebUI\\Port=${webPort}`, 'WebUI\\LocalHostAuth=false', 'WebUI\\HostHeaderValidation=false',
        'WebUI\\CSRFProtection=false', 'Connection\\UPnP=false', 'General\\Locale=en', '',
    ].join('\n'));
    const daemon = spawn(process.env.CJ_QBITTORRENT || 'qbittorrent-nox', [`--profile=${profile}`, `--webui-port=${webPort}`],
                         { stdio: ['ignore', 'ignore', 'inherit'] });
    cleanups.push(() => daemon.kill('SIGKILL'));
    const api = `http://127.0.0.1:${webPort}/api/v2`;
    await until('qBittorrent Web API', async () => (await request(`${api}/app/version`)).status === 200, 30000, 100);

    started = performance.now();
    const added = await request(`${api}/torrents/add`, form({ urls: magnet, savepath: savePath, sequentialDownload: 'true', firstLastPiecePrio: 'true' }));
    if (added.status !== 200 || !/^Ok/.test(added.body.toString()))
        throw new Error(`qBittorrent refused the torrent: ${added.status} ${added.body}`);
    await request(`${api}/torrents/addPeers`, form({ hashes: infoHash, peers: peers.join('|') }));
    const files = await until('metadata', async () => {
        const list = await json(`${api}/torrents/files?hash=${infoHash}`);
        return list.length ? list : undefined;
    }, 60000, 20);
    const props = await json(`${api}/torrents/properties?hash=${infoHash}`);
    const pieceLength = props.piece_size;
    const filePath = path.join(savePath, files[0].name);
    let states = [];
    const refresh = async () => { states = await json(`${api}/torrents/pieceStates?hash=${infoHash}`); };
    const have = (start, end) => {
        for (let p = Math.floor(start / pieceLength); p <= Math.floor(end / pieceLength); ++p)
            if (states[p] !== 2)
                return false;
        return true;
    };
    await refresh();
    return {
        length: files[0].size,
        stream: async function* (start, end) {
            let handle;
            try {
                for (let at = start; at <= end; at += chunk) {
                    const last = Math.min(at + chunk - 1, end);
                    while (!have(at, last)) {
                        await sleep(10);
                        await refresh();
                    }
                    // The file exists once its first piece is written.
                    handle = handle || await fs.promises.open(filePath, 'r');
                    const buffer = Buffer.alloc(last - at + 1);
                    const { bytesRead } = await handle.read(buffer, 0, buffer.length, at);
                    yield buffer.subarray(0, bytesRead);
                }
            } finally {
                if (handle)
                    await handle.close();
            }
        },
        held: at => states[Math.floor(at / pieceLength)] === 2,
        fetchedBytes: async () => (await json(`${api}/torrents/properties?hash=${infoHash}`)).total_downloaded,
        usage: async () => ({ cpuMs: cpuMsOf(daemon.pid), peakProcessBytes: peakBytes(daemon.pid) }),
        close: async () => {
            daemon.kill('SIGTERM');
            await new Promise(resolve => daemon.once('exit', resolve));
        },
    };
}

// Stremio's streaming server in a fresh container per run, on the host's
// network. It finds peers only through trackers or the DHT, so a tracker
// here hands it the seeders' addresses.
async function openStremio() {
    const tracker = http.createServer((req, res) => {
        const compact = Buffer.concat(peers.map(peer => {
            const [host, port] = peer.split(':');
            const entry = Buffer.alloc(6);
            host.split('.').forEach((part, i) => entry.writeUInt8(Number(part), i));
            entry.writeUInt16BE(Number(port), 4);
            return entry;
        }));
        res.end(Buffer.concat([Buffer.from(`d8:intervali30e5:peers${compact.length}:`), compact, Buffer.from('e')]));
    });
    await new Promise(resolve => tracker.listen(0, '127.0.0.1', resolve));
    cleanups.push(() => tracker.close());
    const image = process.env.CJ_STREMIO_IMAGE || 'stremio/server:v4.22.5';
    const name = `cj-bench-stremio-${process.pid}`;
    execFileSync('docker', ['run', '-d', '--rm', '--network', 'host', '--name', name, image], { stdio: 'ignore' });
    cleanups.push(() => execFileSync('docker', ['rm', '-f', name], { stdio: 'ignore' }));
    const pid = Number(execFileSync('docker', ['inspect', '-f', '{{.State.Pid}}', name]).toString().trim());
    const server = 'http://127.0.0.1:11470';
    await until('Stremio server', async () => (await request(`${server}/stats.json`)).status === 200, 60000, 200);

    started = performance.now();
    const body = JSON.stringify({
        torrent: { infoHash },
        peerSearch: { sources: [`tracker:http://127.0.0.1:${tracker.address().port}/announce`], min: 1, max: 50 },
        guessFileIdx: {},
    });
    const stats = await json(`${server}/${infoHash}/create`, { method: 'POST', body, headers: { 'content-type': 'application/json' } });
    const url = `${server}/${infoHash}/0`;
    return {
        length: stats.files[0].length,
        stream: (start, end) => httpRange(url, start, end),
        held: () => null,  // the server does not say which pieces it has
        fetchedBytes: async () => (await json(`${server}/${infoHash}/stats.json`)).downloaded,
        usage: async () => ({ cpuMs: cpuMsOf(pid), peakProcessBytes: peakBytes(pid) }),
        close: async () => {},
    };
}

async function readRange(client, at, length) {
    let bytes = 0;
    for await (const data of await client.stream(at, at + length - 1))
        bytes += data.length;
    if (bytes !== length)
        throw new Error(`Short read at ${at}: ${bytes} of ${length}`);
}

async function main() {
    fs.mkdirSync(cache, { recursive: true });
    const client = engineName === 'qbittorrent' ? await openQBittorrent()
                 : engineName === 'stremio' ? await openStremio()
                 : await openLibrary();
    const result = { engine: engineName, ramOnly: false, openMs: Math.round(performance.now() - started) };
    await readRange(client, 0, 65536);
    result.firstByteMs = Math.round(performance.now() - started);

    // Playback reads one stream from the start, as a player reading over
    // HTTP does (Stremio's server hands its stream to the player). A new
    // stream per chunk would throw away each client's read-ahead:
    // torrent-stream then fetched only the chunk being read and stalled
    // about 2 s on a plain loopback seeder.
    const playStarted = performance.now();
    let stalls = 0, stallMs = 0, longest = 0;
    const played = Math.min(client.length / 2, 48 * MiB);
    const stream = await client.stream(0, played - 1);
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
    if (pull.return)
        await pull.return();
    if (stream.destroy)
        stream.destroy();
    Object.assign(result, { playedBytes: played, playStalls: stalls, playStallMs: Math.round(stallMs), longestStallMs: Math.round(longest) });

    result.seeks = [];
    for (const fraction of [.02, .45, .10, .55, .30, .90, .05, .60, .20, .75, .40, .95, .15, .65, .35, .85, .25, .70, .48, .80]) {
        const at = Math.floor(client.length * fraction / chunk) * chunk;
        const held = client.held(at);
        const start = performance.now();
        await readRange(client, at, chunk);
        result.seeks.push({ at, kind: held === null ? 'unknown' : held ? 'cached' : 'missing', ms: Math.round(performance.now() - start) });
        await sleep(200);
    }
    result.fetchedBytes = await client.fetchedBytes();
    Object.assign(result, await client.usage());
    result.totalMs = Math.round(performance.now() - started);
    console.log(JSON.stringify(result));
    clearTimeout(timeout);
    await client.close();
    cleanUp();
    process.exit(0);
}
main().catch(error => fail(error && error.stack || String(error), 1));
