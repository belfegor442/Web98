/**
 * Web98 memory/CPU benchmark: per-process breakdown, five simple pages,
 * background-tab throttling, repeated tab churn (leak check).
 * Run: npm run bench:memory
 *
 * Reports working set (WS), private WS (per-process, exclusive) and shared
 * (WS - private) per process role, plus summed CPU over measurement windows.
 * Machine-readable JSON with all metrics is written next to this script.
 */

const path = require('path');
const os = require('os');
const fs = require('fs');
const { spawn, execFileSync } = require('child_process');
const puppeteer = require('puppeteer-core');

const ELECTRON = path.join(__dirname, '..', '..', 'node_modules', 'electron', 'dist', 'electron.exe');
const ROOT = path.join(__dirname, '..', '..');
const PORT = 9365;
const PROFILE = path.join(os.tmpdir(), 'web98-mem-profile');
const FIXTURES = path.join(ROOT, 'test', 'fixtures');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function ps(script) {
  return execFileSync(
    'powershell.exe',
    ['-NoProfile', '-NonInteractive', '-Command', script],
    { encoding: 'utf8', windowsHide: true }
  );
}

/** Raise the app window so visibility measurements are not occluded. */
function raiseAppWindow(pid) {
  ps('$w = New-Object -ComObject WScript.Shell; [void]$w.AppActivate(' + pid + '); Start-Sleep -Milliseconds 200; [void]$w.AppActivate(' + pid + ')');
}

/** All electron.exe processes with role classification (from --type= switches). */
function electronProcesses() {
  const out = ps(
    "$ErrorActionPreference='SilentlyContinue'; " +
    "Get-CimInstance Win32_Process -Filter \"Name='electron.exe'\" | " +
    "ForEach-Object { $_.ProcessId.ToString() + '|' + $_.ParentProcessId + '|' + ($_.CommandLine -replace '\\s+',' ') }"
  );
  return out.split(/\r?\n/).filter(Boolean).map((line) => {
    const i1 = line.indexOf('|');
    const i2 = line.indexOf('|', i1 + 1);
    const pid = parseInt(line.slice(0, i1), 10);
    const parent = parseInt(line.slice(i1 + 1, i2), 10);
    const cmd = line.slice(i2 + 1) || '';
    let role = 'main';
    const m = /--type=([a-z-]+)/.exec(cmd);
    if (m) role = m[1];
    return { pid, parent, role };
  });
}

/** Memory (working set + private) for the given pids. */
function memoryOf(pids) {
  if (!pids.length) return [];
  const filter = pids.map((p) => `($_.Id -eq ${p})`).join(' -or ');
  const out = ps(
    `$ErrorActionPreference='SilentlyContinue'; Get-Process | Where-Object { ${filter} } | ` +
    'ForEach-Object { $_.Id.ToString() + "|" + $_.WorkingSet64 + "|" + $_.PrivateMemorySize64 }'
  );
  return out.split(/\r?\n/).filter(Boolean).map((line) => {
    const [pid, ws, priv] = line.split('|').map(Number);
    return { pid, wsMB: +(ws / 1048576).toFixed(1), privMB: +(priv / 1048576).toFixed(1) };
  });
}

function cpuSeconds(pids) {
  if (!pids.length) return 0;
  const filter = pids.map((p) => `($_.Id -eq ${p})`).join(' -or ');
  const out = ps(
    `$ErrorActionPreference='SilentlyContinue'; (Get-Process | Where-Object { ${filter} } | ` +
    'Measure-Object -Property CPU -Sum).Sum'
  );
  const n = parseFloat(String(out).trim().replace(',', '.'));
  return Number.isFinite(n) ? n : 0;
}

function breakdown(rootPid) {
  const procs = electronProcesses().filter((p) => isDescendant(p, rootPid) || p.pid === rootPid);
  const mem = memoryOf(procs.map((p) => p.pid));
  const byPid = new Map(mem.map((m) => [m.pid, m]));
  const rows = procs.map((p) => {
    const m = byPid.get(p.pid) || { wsMB: 0, privMB: 0 };
    return { pid: p.pid, role: p.role, wsMB: m.wsMB, privMB: m.privMB, sharedMB: +(m.wsMB - m.privMB).toFixed(1) };
  });
  const total = rows.reduce(
    (a, r) => ({ wsMB: +(a.wsMB + r.wsMB).toFixed(1), privMB: +(a.privMB + r.privMB).toFixed(1) }),
    { wsMB: 0, privMB: 0 }
  );
  total.sharedMB = +(total.wsMB - total.privMB).toFixed(1);
  return { rows, total, count: rows.length };
}

function isDescendant(p, rootPid) {
  const map = new Map();
  electronProcessCache.forEach((x) => map.set(x.pid, x.parent));
  let cur = p.parent;
  let guard = 0;
  while (cur && cur !== rootPid && guard++ < 20) cur = map.get(cur);
  return cur === rootPid;
}

let electronProcessCache = [];

/** CPU percent (1 core = 100) summed over the tree during [t0,t1]. */
function cpuPercentOver(pids, ms) {
  const t0 = Date.now();
  const c0 = cpuSeconds(pids);
  const spinUntil = t0 + ms;
  return (async () => {
    while (Date.now() < spinUntil) await sleep(Math.min(200, spinUntil - Date.now()));
    const c1 = cpuSeconds(pids);
    return +(((c1 - c0) * 100) / ((Date.now() - t0) / 1000)).toFixed(1);
  })();
}

function treePids(rootPid) {
  electronProcessCache = electronProcesses();
  return electronProcessCache
    .filter((p) => p.pid === rootPid || isDescendant(p, rootPid))
    .map((p) => p.pid);
}

function env() {
  return Object.assign({}, process.env, { WEB98_USER_DATA: PROFILE });
}

function envInfo() {
  const cpus = os.cpus();
  return {
    cpu: cpus[0] ? cpus[0].model.trim() : 'unknown',
    cores: cpus.length,
    totalMemGB: +(os.totalmem() / 1073741824).toFixed(1),
    freeMemGB: +(os.freemem() / 1073741824).toFixed(1),
    platform: process.platform + ' ' + os.release(),
    node: process.version,
    electron: require(path.join(ROOT, 'node_modules', 'electron', 'package.json')).version
  };
}

function fmtRows(bd, indent) {
  const byRole = new Map();
  bd.rows.forEach((r) => {
    const cur = byRole.get(r.role) || { wsMB: 0, privMB: 0, n: 0 };
    cur.wsMB += r.wsMB;
    cur.privMB += r.privMB;
    cur.n += 1;
    byRole.set(r.role, cur);
  });
  const lines = [];
  Array.from(byRole.keys()).sort().forEach((role) => {
    const v = byRole.get(role);
    lines.push(
      indent + role.padEnd(10) +
      ' n=' + String(v.n).padEnd(3) +
      ' ws=' + v.wsMB.toFixed(1) + ' MB' +
      ' priv=' + v.privMB.toFixed(1) + ' MB'
    );
  });
  lines.push(indent + 'TOTAL      n=' + bd.count + ' ws=' + bd.total.wsMB + ' MB priv=' + bd.total.privMB + ' MB');
  return lines;
}

async function waitForDebugger(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const res = await fetch(`http://127.0.0.1:${PORT}/json/version`);
      if (res.ok) return res.json();
    } catch (e) { /* retry */ }
    await sleep(250);
  }
  throw new Error('debugger did not start');
}

async function connectChrome(browser) {
  let deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const pages = await browser.pages();
    const chrome = pages.find((p) => p.url().includes('index.html'));
    if (chrome) return chrome;
    await sleep(150);
  }
  throw new Error('chrome page not found');
}

function fixtureUrl(n) {
  return 'file:///' + path.join(FIXTURES, 'page' + n + '.html').replace(/\\/g, '/');
}

async function openTab(browser, chrome, url) {
  await chrome.evaluate((u) => window.web98.invoke('tabs:create', { url: u }), url);
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const pages = await browser.pages();
    if (pages.some((p) => p.url() === url || p.url() === url + '#')) return true;
    await sleep(150);
  }
  return false;
}

async function closeAllButFirst(browser, chrome) {
  for (let i = 0; i < 40; i++) {
    const n = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    if (n <= 1) break;
    await chrome.evaluate(() => {
      const btn = document.querySelector('.ow-tab.is-active .ow-tab__close') ||
        document.querySelector('.ow-tab__close');
      if (btn) btn.click();
    });
    await sleep(200);
  }
  await sleep(1500);
}

async function main() {
  const results = { env: envInfo(), when: new Date().toISOString(), scenarios: {} };
  let proc = null;
  let browser = null;

  // Never terminate unrelated Electron applications; this benchmark owns only its child process.

  try {
    /* ---- 1. startup + idle ---- */
    proc = spawn(ELECTRON, ['.', `--remote-debugging-port=${PORT}`], {
      cwd: ROOT,
      stdio: ['ignore', 'pipe', 'pipe'],
      env: env()
    });
    const t0 = Date.now();
    await waitForDebugger(30000);
    browser = await puppeteer.connect({
      browserURL: `http://127.0.0.1:${PORT}`,
      defaultViewport: null
    });
    const chrome = await connectChrome(browser);
    await sleep(3000); // warm-up: let startup work settle

    const rootPid = proc.pid;
    let pids = treePids(rootPid);
    const idleMem = breakdown(rootPid);
    const idleCpu = await cpuPercentOver(pids, 6000);
    results.scenarios.idle = {
      startupMs: Date.now() - t0,
      procs: idleMem.count,
      total: idleMem.total,
      cpuPercent: idleCpu,
      rows: idleMem.rows
    };

    /* ---- 2. five simple pages ---- */
    const pagesOk = [];
    for (let n = 1; n <= 5; n++) pagesOk.push(await openTab(browser, chrome, fixtureUrl(n)));
    await sleep(3000); // settle after loads
    pids = treePids(rootPid);
    const fiveMem = breakdown(rootPid);
    const fiveCpuIdle = await cpuPercentOver(pids, 5000);
    results.scenarios.fivePages = {
      pagesLoaded: pagesOk.filter(Boolean).length,
      procs: fiveMem.count,
      total: fiveMem.total,
      cpuPercent: fiveCpuIdle,
      rows: fiveMem.rows
    };

    /* ---- 3. background tab throttling (ticker page) ---- */
    const ticker = 'file:///' + path.join(FIXTURES, 'ticker.html').replace(/\\/g, '/');
    await chrome.evaluate((u) => window.web98.invoke('tabs:create', { url: u }), ticker);
    await sleep(1200);
    const tickerPage = (await browser.pages()).find((p) => p.url().includes('ticker.html'));
    if (tickerPage) {
      raiseAppWindow(rootPid);
      await tickerPage.bringToFront();
      await sleep(600);
      await tickerPage.evaluate(() => { window.__ticks = 0; });
      await sleep(3000);
      const visibleTicks = await tickerPage.evaluate(() => window.__ticks);
      if (visibleTicks < 20) {
        // window did not actually come to the foreground — try once more
        raiseAppWindow(rootPid);
        await tickerPage.bringToFront();
        await sleep(600);
        await tickerPage.evaluate(() => { window.__ticks = 0; });
        await sleep(3000);
      }
      const visibleTicksFinal = await tickerPage.evaluate(() => window.__ticks);
      // switch back to the first tab so the ticker is hidden
      await chrome.evaluate(() => {
        const rows = document.querySelectorAll('.ow-tab');
        if (rows[0]) rows[0].dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0 }));
      });
      await sleep(600);
      await tickerPage.evaluate(() => { window.__ticks = 0; });
      const hiddenCpuStart = cpuSeconds(treePids(rootPid));
      const h0 = Date.now();
      await sleep(4000);
      const hiddenTicks = await tickerPage.evaluate(() => window.__ticks);
      const hiddenCpu = +(((cpuSeconds(treePids(rootPid)) - hiddenCpuStart) * 100) / ((Date.now() - h0) / 1000)).toFixed(1);
      results.scenarios.throttle = {
        visibleTicks3s: visibleTicksFinal,
        hiddenTicks4s: hiddenTicks,
        hiddenCpuPercent: hiddenCpu
      };
    }

    /* ---- 4. tab churn: repeated create/close, leak check ---- */
    const churn = [];
    for (let round = 1; round <= 3; round++) {
      for (let i = 0; i < 5; i++) {
        await chrome.evaluate(() => window.web98.invoke('tabs:create', {}));
        await sleep(120);
      }
      await sleep(800);
      await closeAllButFirst(browser, chrome);
      pids = treePids(rootPid);
      const bd = breakdown(rootPid);
      churn.push({ round, procs: bd.count, total: bd.total });
      await sleep(500);
    }
    results.scenarios.churn = churn;

    /* ---- 5. back to one tab: final idle ---- */
    await sleep(2000);
    pids = treePids(rootPid);
    const finalMem = breakdown(rootPid);
    const finalCpu = await cpuPercentOver(pids, 5000);
    results.scenarios.finalIdle = {
      procs: finalMem.count,
      total: finalMem.total,
      cpuPercent: finalCpu,
      rows: finalMem.rows
    };
  } finally {
    if (browser) { try { await browser.disconnect(); } catch (e) { /* ignore */ } }
    if (proc && proc.exitCode === null) {
      try {
        execFileSync('taskkill', ['/F', '/PID', String(proc.pid), '/T'], { stdio: 'ignore', windowsHide: true });
      } catch (e) { /* ignore */ }
    }
  }

  /* ------------------------------ report ------------------------------ */
  const s = results.scenarios;
  console.log('Web98 memory/CPU benchmark');
  console.log('  machine: ' + results.env.cpu + ' x' + results.env.cores +
    ', ' + results.env.totalMemGB + ' GB RAM, ' + results.env.platform);
  console.log('  profile: ' + PROFILE + ' (persistent, warm), warm-up 3s after startup');
  console.log('');
  console.log('  idle (1 tab, settled):');
  fmtRows({ rows: s.idle.rows, total: s.idle.total, count: s.idle.procs }, '    ').forEach((l) => console.log(l));
  console.log('    cpu=' + s.idle.cpuPercent + ' % (1 core = 100)');
  console.log('');
  console.log('  five simple pages (' + s.fivePages.pagesLoaded + '/5 loaded, settled 3s):');
  fmtRows({ rows: s.fivePages.rows, total: s.fivePages.total, count: s.fivePages.procs }, '    ').forEach((l) => console.log(l));
  console.log('    cpu=' + s.fivePages.cpuPercent + ' % (1 core = 100)');
  if (s.throttle) {
    console.log('');
    console.log('  background throttling: visible ticker ' + s.throttle.visibleTicks3s +
      ' ticks/3s -> hidden ' + s.throttle.hiddenTicks4s + ' ticks/4s, hidden cpu=' +
      s.throttle.hiddenCpuPercent + ' %');
  }
  console.log('');
  console.log('  tab churn (create 5 + close, 3 rounds):');
  s.churn.forEach((c) => console.log('    round ' + c.round + ': procs=' + c.procs +
    ' ws=' + c.total.wsMB + ' MB priv=' + c.total.privMB + ' MB'));
  console.log('');
  console.log('  final idle (back to 1 tab):');
  fmtRows({ rows: s.finalIdle.rows, total: s.finalIdle.total, count: s.finalIdle.procs }, '    ').forEach((l) => console.log(l));
  console.log('    cpu=' + s.finalIdle.cpuPercent + ' % (1 core = 100)');

  const outFile = path.join(__dirname, 'memory-results.json');
  fs.writeFileSync(outFile, JSON.stringify(results, null, 2));
  console.log('');
  console.log('  json: ' + outFile);

  /* checks: leaks (process count) and throttling behavior */
  let failures = 0;
  const expect = (ok, msg) => {
    console.log((ok ? '  PASS  ' : '  FAIL  ') + msg);
    if (!ok) failures++;
  };
  expect(s.fivePages.pagesLoaded === 5, 'all 5 simple pages loaded');
  expect(s.churn[2].procs <= s.churn[0].procs, 'no process growth across churn rounds (' +
    s.churn[0].procs + ' -> ' + s.churn[2].procs + ')');
  expect(s.finalIdle.procs <= s.idle.procs, 'closing tabs releases processes (' +
    s.idle.procs + ' -> ' + s.finalIdle.procs + ')');
  if (s.throttle) {
    expect(s.throttle.hiddenTicks4s < s.throttle.visibleTicks3s,
      'background ticker is throttled when hidden');
  }
  expect(s.idle.cpuPercent <= 3, 'idle cpu within 0-3% target (' + s.idle.cpuPercent + ' %)');
  console.log(failures ? '\n' + failures + ' check(s) FAILED' : '\nALL MEMORY CHECKS PASSED');
  process.exit(failures ? 1 : 0);
}

main().catch((err) => {
  console.error('bench:memory fatal:', err);
  process.exit(1);
});
