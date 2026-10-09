/**
 * Web98 performance benchmark: cold startup, idle memory, tab-switch
 * latency, navigation latency. Run: npm run bench
 */

const path = require('path');
const { spawn } = require('child_process');
const puppeteer = require('puppeteer-core');

const ELECTRON = path.join(__dirname, '..', '..', 'node_modules', 'electron', 'dist', 'electron.exe');
const ROOT = path.join(__dirname, '..', '..');
const PORT = 9355;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

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

  /* --- 1. cold startup (wall time spawn -> debugger + ready log) --- */
  let t0 = Date.now();
  const proc = spawn(ELECTRON, ['.', `--remote-debugging-port=${PORT}`], {
    cwd: ROOT,
    stdio: ['ignore', 'pipe', 'pipe']
  });
  let readyMs = null;
  proc.stdout.on('data', (d) => {
    const m = /ready in (\d+) ms/.exec(String(d));
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
  await sleep(1000);
  results.appReadyMs = readyMs; // main-process reported
  results.uiInteractiveMs = Date.now() - t0;

  /* --- 2. idle memory after settle --- */
  await sleep(1500);
  const stats = await chrome.evaluate(() => window.web98.invoke('app:stats'));
  results.idleRssMB = stats.rssMB;
  results.idleHeapMB = stats.heapMB;

  /* --- 3. tab switch latency (5 tabs, 20 switches) --- */
  for (let i = 0; i < 4; i++) {
    await pressCtrl(chrome, 'KeyT', 84);
    await sleep(350);
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

  /* --- 4. navigation latency (address bar -> committed, local page x5) --- */
  const navTimes = [];
  const target = 'file:///' + path.join(ROOT, 'src', 'renderer', 'startpage.html').replace(/\\/g, '/');
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

  /* --- 5. memory after activity --- */
  const stats2 = await chrome.evaluate(() => window.web98.invoke('app:stats'));
  results.activeRssMB = stats2.rssMB;

  console.log('Web98 benchmark');
  console.log('  startup (debugger up):   ' + results.debuggerUpMs + ' ms');
  console.log('  startup (app "ready"):   ' + results.appReadyMs + ' ms');
  console.log('  startup (UI attached):   ' + results.uiInteractiveMs + ' ms');
  console.log('  idle RSS:                ' + results.idleRssMB + ' MB (heap ' + results.idleHeapMB + ' MB)');
  console.log('  tab switch median/max:   ' + results.tabSwitchMedianMs + ' / ' + results.tabSwitchMaxMs + ' ms  (n=20)');
  console.log('  local nav median:        ' + results.localNavMedianMs + ' ms  (n=5)');
  console.log('  RSS after 5 tabs:        ' + results.activeRssMB + ' MB');

  try { await browser.disconnect(); } catch (e) { /* ignore */ }
  proc.kill();
  await sleep(400);
  process.exit(0);
}

main().catch((err) => {
  console.error('bench fatal:', err);
  process.exit(1);
});
