/**
 * Web98 CEF performance benchmark: cold startup, N-tab session restore,
 * memory, idle CPU, process breakdown. Run: npm run bench:cef
 * Options: --tabs=10 --runs=3 --delay=250 --port=9456 --dport=9556 --fresh
 * Uses an isolated profile (WEB98_USER_DATA) and a local HTTP server with
 * per-page latency so restore timing reflects real navigation, not disk cache.
 * The bench HTTP port and the DevTools debug port must be different.
 */

const path = require('path');
const os = require('os');
const fs = require('fs');
const http = require('http');
const { spawn, execSync } = require('child_process');
const { DatabaseSync } = require('node:sqlite');

const EXE = path.join(__dirname, '..', '..', 'native', 'build', 'Release', 'web98.exe');
const PROFILE = path.join(os.tmpdir(), 'web98-bench-cef-profile');

const args = {};
for (const a of process.argv.slice(2)) {
  const m = /^--([^=]+)(?:=(.*))?$/.exec(a);
  if (m) args[m[1]] = m[2] !== undefined ? m[2] : true;
}
const TABS = parseInt(args.tabs, 10) || 10;
const RUNS = parseInt(args.runs, 10) || 3;
const DELAY = parseInt(args.delay, 10) || 250;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

if (!fs.existsSync(EXE)) { console.error('web98.exe not found: ' + EXE); process.exit(1); }
if (args.fresh) fs.rmSync(PROFILE, { recursive: true, force: true });

function seedProfile(urls) {
  fs.mkdirSync(PROFILE, { recursive: true });
  const db = new DatabaseSync(path.join(PROFILE, 'web98.db'));
  db.exec("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);");
  db.prepare('DELETE FROM settings WHERE key = ?').run('lastSession');
  db.prepare('INSERT INTO settings (key, value) VALUES (?, ?)')
    .run('lastSession', JSON.stringify({ windows: [{ urls, active: 0 }] }));
  db.close();
}

function startServer(port) {
  return new Promise((resolve, reject) => {
    const srv = http.createServer((req, res) => {
      const n = (req.url.match(/\d+/) || ['0'])[0];
      setTimeout(() => {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(
          '<!doctype html><html><head><title>Bench page ' + n + '</title></head>' +
          '<body style="font-family:sans-serif"><h1>Bench page ' + n + '</h1>' +
          '<p>' + 'lorem ipsum '.repeat(50) + '</p></body></html>');
      }, DELAY);
    });
    srv.on('error', reject);
    srv.listen(port, '127.0.0.1', () => resolve(srv));
  });
}

async function fetchTargets(port) {
  try {
    const r = await fetch('http://127.0.0.1:' + port + '/json/list');
    return await r.json();
  } catch (e) { return null; }
}

// PowerShell via -EncodedCommand: immune to cmd.exe quote mangling (the
// scripts contain nested " ... ' ... " that always break -Command).
function psRun(script) {
  const full = "$ProgressPreference='SilentlyContinue';" + script;
  const b64 = Buffer.from(full, 'utf16le').toString('base64');
  const out = execSync('powershell -NoProfile -EncodedCommand ' + b64).toString();
  return out.replace(/#< CLIXML[\s\S]*?<\/Objs>/g, '').trim();
}

function killStrays() {
  try {
    psRun(
      "Get-CimInstance Win32_Process -Filter \"Name='web98.exe'\" | " +
      "Where-Object { $_.CommandLine -like '*web98-bench-cef-profile*' } | " +
      'ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }');
  } catch (e) {}
}

function psVal(script) {
  try { return parseFloat(psRun(script)); } catch (e) { return NaN; }
}

function cpuPercentDuring(ms) {
  const read =
    "(Get-Process web98 -ErrorAction SilentlyContinue | ForEach-Object { $_.TotalProcessorTime.TotalMilliseconds } | Measure-Object -Sum).Sum";
  const c1 = psVal(read), w1 = Date.now();
  return sleep(ms).then(() => {
    const c2 = psVal(read), w2 = Date.now();
    const wall = (w2 - w1) / 1000;
    if (!(c1 >= 0) || !(c2 >= c1) || wall <= 0) return NaN;
    return Math.round(((c2 - c1) / 1000 / wall) * 1000) / 10;
  });
}

function processBreakdown() {
  // One PS call returns raw cmdlines; classification happens in JS (nested
  // Where-Object patterns with -notmatch/gpu-process returned empty output).
  const out = psRun(
    "Get-CimInstance Win32_Process -Filter \"Name='web98.exe'\" | " +
    "Where-Object { $_.ExecutablePath -like '*build\\Release*' } | " +
    'ForEach-Object { $_.CommandLine }');
  const lines = out.split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
  const n = (fn) => lines.filter(fn).length;
  return {
    all: lines.length,
    browser: n((l) => l.indexOf('--type=') === -1),
    renderer: n((l) => l.indexOf('--type=renderer') !== -1),
    gpu: n((l) => l.indexOf('--type=gpu-process') !== -1),
    utility: n((l) => l.indexOf('--type=utility') !== -1),
  };
}

function ramMB() {
  return psVal("(Get-Process web98 -ErrorAction SilentlyContinue | Measure-Object WorkingSet64 -Sum).Sum/1MB");
}

async function runOnce(port, dport, urls, withIdle) {
  killStrays();
  await sleep(400);
  seedProfile(urls);
  const t0 = Date.now();
  const child = spawn(EXE, ['--remote-debugging-port=' + dport], {
    env: Object.assign({}, process.env, { WEB98_USER_DATA: PROFILE }),
    stdio: 'ignore',
  });
  let chromeMs = null;
  let restoreMs = null;
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline && restoreMs === null) {
    const t = await fetchTargets(dport);
    if (Array.isArray(t)) {
      const pages = t.filter((x) => x.type === 'page');
      if (chromeMs === null && pages.some((x) => x.url.indexOf('index.html') !== -1)) {
        chromeMs = Date.now() - t0;
      }
      const done = pages.filter((x) => x.url.indexOf('http://127.0.0.1:' + port + '/') === 0).length;
      if (done >= urls.length) restoreMs = Date.now() - t0;
    }
    await sleep(50);
  }
  if (restoreMs === null) {
    killStrays();
    throw new Error('restore did not complete within 60s');
  }
  await sleep(1500);  // let processes settle after load
  const mem = ramMB();
  const procs = processBreakdown();
  let cpuIdle = null;
  if (withIdle) cpuIdle = await cpuPercentDuring(10000);
  try { execSync('taskkill /F /T /PID ' + child.pid, { stdio: 'ignore' }); } catch (e) {}
  killStrays();
  return { chromeMs, restoreMs, mem, procs, cpuIdle };
}

async function main() {
  let port = parseInt(args.port, 10) || 9456;
  let dport = parseInt(args.dport, 10) || 9556;
  if (dport === port) { console.error('--dport must differ from --port'); process.exit(1); }
  let srv = null;
  for (let i = 0; i < 10 && !srv; i++) {
    try { srv = await startServer(port + i); } catch (e) { srv = null; port++; }
  }
  if (!srv) { console.error('no free port'); process.exit(1); }

  const urls = [];
  for (let i = 1; i <= TABS; i++) urls.push('http://127.0.0.1:' + port + '/page' + i + '.html');
  console.log('bench: tabs=' + TABS + ' runs=' + RUNS + ' delay=' + DELAY + 'ms port=' + port +
    ' dport=' + dport + ' profile=' + PROFILE);

  // Warm-up run (profile/schema/cache creation) is not counted.
  await runOnce(port, dport, urls, false);
  console.log('warmup done');

  const results = [];
  for (let r = 1; r <= RUNS; r++) {
    const last = r === RUNS;
    const x = await runOnce(port, dport, urls, last);
    results.push(x);
    console.log('run ' + r + '/' + RUNS +
      ': chrome=' + x.chromeMs + 'ms restore=' + x.restoreMs + 'ms ram=' +
      x.mem.toFixed(1) + 'MB procs=' + JSON.stringify(x.procs) +
      (last && x.cpuIdle !== null ? ' idleCpu=' + x.cpuIdle + '%' : ''));
  }

  const avg = (k) => results.reduce((s, x) => s + x[k], 0) / results.length;
  const best = (k) => Math.min.apply(null, results.map((x) => x[k]));
  console.log('--- summary ---');
  console.log('restore avg=' + avg('restoreMs').toFixed(0) + 'ms best=' + best('restoreMs') + 'ms' +
    ' | chrome avg=' + avg('chromeMs').toFixed(0) + 'ms' +
    ' | ram avg=' + avg('mem').toFixed(1) + 'MB' +
    ' | idleCpu=' + (results[results.length - 1].cpuIdle) + '%' +
    ' | procs=' + JSON.stringify(results[results.length - 1].procs));
  srv.close();
}

main().catch((e) => { console.error(e.message || e); killStrays(); process.exit(1); });
