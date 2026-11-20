// Downloads one whole torrent with another client, for tst_benchdownload:
//   node download_competitor.cjs <magnet> <folder>
// The magnet names the benchmark's tracker, which lists the local seeders.
// CJ_COMPETITOR picks the client:
//   aria2         aria2c
//   transmission  transmission-cli (libtransmission)
//   qbittorrent   qbittorrent-nox (libtorrent), through its Web API
//   webtorrent    WebTorrent 3, in this process
// CJ_COMPETITOR_MODULES is the node_modules folder holding webtorrent.
//
// Every client runs with DHT, LSD, PEX, uTP and port mapping off, so it
// finds peers only through the tracker, as Corporal Jackie does in the
// benchmark. The clock starts when the torrent is added, with the client
// already running where it is a daemon. Prints {"ms": n} when the file is
// complete on disk.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const net = require('node:net');
const { spawn } = require('node:child_process');
const { performance } = require('node:perf_hooks');
const { pathToFileURL } = require('node:url');

const [magnet, folder] = process.argv.slice(2);
const engine = process.env.CJ_COMPETITOR || 'aria2';
const modules = path.resolve(process.env.CJ_COMPETITOR_MODULES || 'node_modules');
const infoHash = /urn:btih:([0-9a-fA-F]{40})/.exec(magnet)[1].toLowerCase();
const sleep = ms => new Promise(r => setTimeout(r, ms));
const cleanups = [];
const cleanUp = () => { while (cleanups.length) try { cleanups.pop()(); } catch (e) {} };
const fail = (message) => { console.error(message); cleanUp(); process.exit(1); };
setTimeout(() => fail(`${engine} timed out`), 280000);
fs.mkdirSync(folder, { recursive: true });

function freePort() {
    return new Promise(resolve => {
        const s = net.createServer().listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); });
    });
}
function request(url, options = {}) {
    return new Promise((resolve, reject) => {
        const req = http.request(url, { method: options.method || 'GET', headers: options.headers || {} }, res => {
            const parts = [];
            res.on('data', d => parts.push(d));
            res.on('end', () => resolve({ status: res.statusCode, body: Buffer.concat(parts) }));
        });
        req.on('error', reject);
        if (options.body) req.write(options.body);
        req.end();
    });
}
// With a length: qBittorrent's server does not read chunked bodies.
const form = fields => {
    const body = new URLSearchParams(fields).toString();
    return { method: 'POST', body, headers: { 'content-type': 'application/x-www-form-urlencoded',
                                              'content-length': Buffer.byteLength(body) } };
};

async function aria2() {
    const started = performance.now();
    const p = spawn('aria2c', ['-d', folder, '--seed-time=0', '--enable-dht=false', '--enable-dht6=false',
                               '--bt-enable-lpd=false', '--enable-peer-exchange=false', '--file-allocation=none',
                               '--bt-save-metadata=false', '--summary-interval=0', '--console-log-level=warn',
                               '--bt-stop-timeout=0', '--max-overall-upload-limit=1K', magnet],
                    { stdio: ['ignore', 'ignore', 'inherit'] });
    cleanups.push(() => p.kill('SIGKILL'));
    const code = await new Promise(r => p.on('exit', r));
    if (code !== 0) throw new Error(`aria2c exited with ${code}`);
    return performance.now() - started;
}

async function transmission() {
    const config = path.join(folder, '..', 'transmission-config');
    fs.mkdirSync(config, { recursive: true });
    fs.writeFileSync(path.join(config, 'settings.json'), JSON.stringify({
        'dht-enabled': false, 'lpd-enabled': false, 'pex-enabled': false, 'utp-enabled': false,
        'port-forwarding-enabled': false, 'rename-partial-files': true, 'download-dir': folder,
        'incomplete-dir-enabled': false, 'peer-port-random-on-start': true, 'ratio-limit-enabled': true, 'ratio-limit': 0,
    }));
    const done = path.join(folder, '..', 'transmission-done');
    const script = path.join(folder, '..', 'transmission-done.sh');
    fs.writeFileSync(script, `#!/bin/sh\ntouch '${done}'\n`, { mode: 0o755 });
    const started = performance.now();
    const out = fs.openSync(path.join(folder, '..', 'transmission.log'), 'w');
    const p = spawn('transmission-cli', ['-g', config, '-w', folder, '-f', script, '-M', '-et', '-D', magnet],
                    { stdio: ['ignore', out, out] });
    cleanups.push(() => p.kill('SIGKILL'));
    while (!fs.existsSync(done)) {
        if (p.exitCode !== null) throw new Error(`transmission-cli exited with ${p.exitCode}`);
        await sleep(20);
    }
    const ms = performance.now() - started;
    p.kill('SIGKILL');
    return ms;
}

async function qbittorrent() {
    const profile = path.join(folder, '..', 'qbt-profile');
    const configDir = path.join(profile, 'qBittorrent', 'config');
    fs.mkdirSync(configDir, { recursive: true });
    const webPort = await freePort();
    const listenPort = await freePort();
    fs.writeFileSync(path.join(configDir, 'qBittorrent.conf'), [
        '[LegalNotice]', 'Accepted=true', '',
        '[BitTorrent]',
        'Session\\DHTEnabled=false', 'Session\\LSDEnabled=false', 'Session\\PeXEnabled=false',
        'Session\\BTProtocol=TCP', 'Session\\QueueingSystemEnabled=false', 'Session\\AddTrackersEnabled=false',
        `Session\\DefaultSavePath=${folder}`, 'Session\\TempPathEnabled=false', `Session\\Port=${listenPort}`,
        'Session\\RefreshInterval=50', 'Session\\MultiConnectionsPerIp=true',
        '', '[Network]', 'PortForwardingEnabled=false', '',
        '[Preferences]', `WebUI\\Port=${webPort}`, 'WebUI\\LocalHostAuth=false', 'WebUI\\HostHeaderValidation=false',
        'WebUI\\CSRFProtection=false', 'Connection\\UPnP=false', 'General\\Locale=en', '',
    ].join('\n'));
    const daemon = spawn('qbittorrent-nox', [`--profile=${profile}`, `--webui-port=${webPort}`], { stdio: ['ignore', 'ignore', 'ignore'] });
    cleanups.push(() => daemon.kill('SIGKILL'));
    const api = `http://127.0.0.1:${webPort}/api/v2`;
    for (let i = 0; ; ++i) {
        const ok = await request(`${api}/app/version`).then(r => r.status === 200, () => false);
        if (ok) break;
        if (i > 300) throw new Error('qBittorrent Web API did not start');
        await sleep(100);
    }
    const started = performance.now();
    const added = await request(`${api}/torrents/add`, form({ urls: magnet, savepath: folder }));
    if (added.status !== 200 || added.body.toString().trim() !== 'Ok.')
        throw new Error(`qBittorrent refused the torrent: ${added.status} ${added.body}`);
    for (;;) {
        const r = await request(`${api}/torrents/info?hashes=${infoHash}`).catch(() => null);
        const list = r && r.status === 200 ? JSON.parse(r.body.toString()) : [];
        if (list.length && list[0].progress >= 1) break;
        await sleep(20);
    }
    return performance.now() - started;
}

async function webtorrent() {
    const WebTorrent = (await import(pathToFileURL(path.join(modules, 'webtorrent', 'index.js')).href)).default;
    const client = new WebTorrent({ dht: false, lsd: false, utp: false, natUpnp: false, natPmp: false, webSeeds: false });
    cleanups.push(() => client.destroy());
    const started = performance.now();
    await new Promise((resolve, reject) => {
        const t = client.add(magnet, { path: folder });
        t.on('done', resolve);
        t.on('error', reject);
    });
    const ms = performance.now() - started;
    await new Promise(r => client.destroy(r));
    return ms;
}

(async () => {
    const run = { aria2, transmission, qbittorrent, webtorrent }[engine];
    if (!run) fail(`Unknown CJ_COMPETITOR: ${engine}`);
    const ms = await run();
    cleanUp();
    console.log(JSON.stringify({ ms: Math.round(ms) }));
    process.exit(0);
})().catch(e => fail(String(e && e.stack || e)));
