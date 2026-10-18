/**
 * Web98 performance benchmark: cold startup, idle memory, tab-switch
 * latency, navigation latency. Run: npm run bench
 */

const path = require('path');
const os = require('os');
const { spawn, execSync } = require('child_process');
const puppeteer = require('puppeteer-core');

const ELECTRON = path.join(__dirname, '..', '..', 'node_modules', 'electron', 'dist', 'electron.exe');
const ROOT = path.join(__dirname, '..', '..');
const PORT = 9355;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
/* Persistent isolated profile: never touches the user's real data, and
   stays warm across runs so startup numbers are comparable. */
const BENCH_PROFILE = path.join(os.tmpdir(), 'web98-bench-profile');
const BENCH_ENV = Object.assign({}, process.env, { WEB98_USER_DATA: BENCH_PROFILE });

function processTreePids(rootPid) {
  if (!Number.isInteger(rootPid) || rootPid <= 0) return [];
  const script =
    "$ErrorActionPreference='SilentlyContinue'; " +
    "$all=@(Get-CimInstance Win32_Process -Filter \"Name='electron.exe'\"); " +
    "$ids=@([int]" + rootPid + "); " +
    "do { $added=$false; foreach($p in $all) { " +
    "if (($ids -contains [int]$p.ParentProcessId) -and -not ($ids -contains [int]$p.ProcessId)) " +
    "{ $ids += [int]$p.ProcessId; $added=$true } } } while($added); " +
    "$ids -join ','";
  try {
    const out = execSync('powershell -NoProfile -Command "' + script.replace(/"/g, '`"') + '"', { encoding: 'utf8' }).trim();
    return out.split(',').map(Number).filter((id) => Number.isInteger(id) && id > 0);
  } catch (e) {
    return [rootPid];
  }
}

function cpuReadMs(rootPid) {
  const pids = processTreePids(rootPid);
  if (!pids.length) return 0;
  const ps = '(Get-Process -Id ' + pids.join(',') +
    ' -ErrorAction SilentlyContinue | ForEach-Object { $_.TotalProcessorTime.TotalMilliseconds } | Measure-Object -Sum).Sum';
  try {
    return parseFloat(execSync('powershell -NoProfile -Command "' + ps + '"').toString()) || 0;
  } catch (e) {
    return 0;
  }
}

function cpuPercentBetween(c1, w1, c2, w2) {
  const wall = (w2 - w1) / 1000;
  if (wall <= 0) return 0;
  return Math.max(0, Math.round(((c2 - c1) / 1000 / wall) * 1000) / 10);
}

// Whole-app CPU as % of one core (can exceed 100 across cores):
// delta of TotalProcessorTime summed over all electron processes / wall time.
async function electronCpuPercent(durationMs) {
  const c1 = cpuReadMs(proc.pid);
  const w1 = Date.now();
  await sleep(durationMs);
  const c2 = cpuReadMs(proc.pid);
  const w2 = Date.now();
  return cpuPercentBetween(c1, w1, c2, w2);
}

function electronProcessCount(rootPid) {
  return processTreePids(rootPid).length;
}

async function waitForDebugger(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const res = await fetch(`http://127.0.0.1:${PORT}/json/version`);
      if (res.ok) return;
    } catch (e) { /* retry */ }
    await sleep(50);
  }
  throw new Error('debugger did not start');
}

async function pressCtrl(page, code, vk) {
  const cdp = await page.createCDPSession();
  const opts = {
    modifiers: 2,
    key: code.slice(3).toLowerCase(),
    code,
    windowsVirtualKeyCode: vk,
    nativeVirtualKeyCode: vk
  };
  await cdp.send('Input.dispatchKeyEvent', Object.assign({ type: 'rawKeyDown' }, opts));
  await cdp.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, opts));
  await cdp.detach();
}

function median(nums) {
  const s = nums.slice().sort((a, b) => a - b);
  return s[Math.floor(s.length / 2)];
}

async function main() {
  const results = {};
  // Never terminate unrelated Electron applications; this benchmark owns only its child process.

  /* --- 1. cold startup (wall time spawn -> debugger + ready log) --- */
  let t0 = Date.now();
  const proc = spawn(ELECTRON, ['.', `--remote-debugging-port=${PORT}`], {
    cwd: ROOT,
    stdio: ['ignore', 'pipe', 'pipe'],
    env: BENCH_ENV
  });
  let readyMs = null;
  let stdoutBuf = '';
  proc.stdout.on('data', (d) => {
    stdoutBuf += String(d);
    const m = /ready in (\d+) ms/.exec(stdoutBuf);
    if (m && readyMs === null) readyMs = parseInt(m[1], 10);
  });
  await waitForDebugger(30000);
  results.debuggerUpMs = Date.now() - t0;

  const browser = await puppeteer.connect({ browserURL: `http://127.0.0.1:${PORT}`, defaultViewport: null });
  let chrome = null;
  let deadline = Date.now() + 15000;
  while (Date.now() < deadline && !chrome) {
    const pages = await browser.pages();
    chrome = pages.find((p) => p.url().includes('index.html'));
    if (!chrome) await sleep(100);
  }
  if (!chrome) throw new Error('chrome page not found');
  results.chromeLoadMs = Date.now() - t0; // chrome UI document loaded
  await sleep(1000);
  results.appReadyMs = readyMs; // main-process reported
  results.uiSettledMs = Date.now() - t0; // chrome loaded + 1000 ms settle

  /* --- 2. idle memory + idle CPU after settle --- */
  await sleep(1500);
  const stats = await chrome.evaluate(() => window.web98.invoke('app:stats'));
  results.idleRssMB = stats.rssMB;
  results.idleHeapMB = stats.heapMB;
  results.idleCpuPercent = await electronCpuPercent(1500);
  results.procsIdle = electronProcessCount(proc.pid);

  /* --- 3. tab switch latency (5 tabs with real pages, 20 switches) --- */
  const target = 'file:///' + path.join(ROOT, 'src', 'renderer', 'startpage.html').replace(/\\/g, '/');
  const loadC1 = cpuReadMs(proc.pid);
  const loadW1 = Date.now();
  for (let i = 0; i < 4; i++) {
    const before = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    for (let attempt = 0; attempt < 3; attempt++) {
      await pressCtrl(chrome, 'KeyT', 84);
      try {
        await chrome.waitForFunction((n) => document.querySelectorAll('.ow-tab').length === n + 1, { timeout: 2000 }, before);
        break;
      } catch (e) { /* key may be dropped while occluded — retry */ }
    }
    await chrome.waitForFunction((n) => document.querySelectorAll('.ow-tab').length === n + 1, { timeout: 3000 }, before);
    await sleep(150);
    await chrome.evaluate((u, n) => {
      const input = document.getElementById('address');
      input.value = u + '#' + n;
      input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    }, target, i);
    await chrome.waitForFunction(() => {
      const el = document.getElementById('status');
      return el && el.textContent === 'Done';
    }, { timeout: 5000 });
  }
  const switchTimes = [];
  for (let i = 0; i < 20; i++) {
    const s = Date.now();
    await chrome.evaluate((n) => {
      const tabs = document.querySelectorAll('.ow-tab');
      tabs[n % tabs.length].dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0 }));
    }, i);
    await chrome.waitForFunction(() => {
      const active = document.querySelectorAll('.ow-tab.is-active');
      return active.length === 1;
    }, { timeout: 2000 });
    switchTimes.push(Date.now() - s);
  }
  results.tabSwitchMedianMs = median(switchTimes);
  results.tabSwitchMaxMs = Math.max(...switchTimes);
  results.loadCpuPercent = cpuPercentBetween(loadC1, loadW1, cpuReadMs(proc.pid), Date.now());

  /* --- 4. navigation latency (address bar -> committed, local page x5) --- */
  const navTimes = [];
  for (let i = 0; i < 5; i++) {
    const s = Date.now();
    await chrome.evaluate((u) => {
      const input = document.getElementById('address');
      input.value = u;
      input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    }, target);
    await chrome.waitForFunction(() => {
      const el = document.getElementById('status');
      return el && el.textContent === 'Done';
    }, { timeout: 5000 });
    navTimes.push(Date.now() - s);
    await sleep(150);
  }
  results.localNavMedianMs = median(navTimes);

  /* --- 5. memory after activity, then release check --- */
  const stats2 = await chrome.evaluate(() => window.web98.invoke('app:stats'));
  results.activeRssMB = stats2.rssMB;
  results.procs5Tabs = electronProcessCount(proc.pid);

  /* --- 6. close the 4 extra tabs, verify count + resource release --- */
  for (let i = 0; i < 4; i++) {
    await chrome.evaluate(() => {
      const btn = document.querySelector('.ow-tab__close');
      if (btn) btn.click();
    });
    await sleep(250);
  }
  await chrome.waitForFunction(() => document.querySelectorAll('.ow-tab').length === 1, { timeout: 5000 });
  results.tabsAfterClose = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
  await sleep(2000);
  const stats3 = await chrome.evaluate(() => window.web98.invoke('app:stats'));
  results.closedRssMB = stats3.rssMB;
  results.tabCloseReleaseMB = Math.round((results.activeRssMB - results.closedRssMB) * 10) / 10;
  results.procsAfterClose = electronProcessCount(proc.pid);

  console.log('Web98 benchmark');
  console.log('  startup (debugger up):   ' + results.debuggerUpMs + ' ms');
  console.log('  startup (app "ready"):   ' + results.appReadyMs + ' ms');
  console.log('  startup (chrome loaded): ' + results.chromeLoadMs + ' ms');
  console.log('  startup (UI settled):    ' + results.uiSettledMs + ' ms');
  console.log('  idle RSS:                ' + results.idleRssMB + ' MB (heap ' + results.idleHeapMB + ' MB)');
  console.log('  idle CPU (1 core = 100):  ' + results.idleCpuPercent + ' %');
  console.log('  load CPU (1 core = 100):  ' + results.loadCpuPercent + ' %');
  console.log('  electron processes:      idle ' + results.procsIdle + ' / 5 tabs ' + results.procs5Tabs + ' / closed ' + results.procsAfterClose);
  console.log('  tab switch median/max:   ' + results.tabSwitchMedianMs + ' / ' + results.tabSwitchMaxMs + ' ms  (n=20)');
  console.log('  local nav median:        ' + results.localNavMedianMs + ' ms  (n=5)');
  console.log('  RSS after 5 tabs:        ' + results.activeRssMB + ' MB');
  console.log('  RSS after closing:       ' + results.closedRssMB + ' MB (' + results.tabCloseReleaseMB + ' MB released, tabs=' + results.tabsAfterClose + ')');

  try { await browser.disconnect(); } catch (e) { /* ignore */ }
  try {
    execSync(`taskkill /F /PID ${proc.pid} /T`, { stdio: 'ignore' });
  } catch (e) {
    proc.kill();
  }
  await sleep(600);
  process.exit(0);
}

main().catch((err) => {
  console.error('bench fatal:', err);
  process.exit(1);
});
