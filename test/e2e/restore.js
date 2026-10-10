/**
 * Session restore e2e: verifies tabs are saved on graceful quit, restored
 * instantly as tab strips on relaunch, and background tabs load staggered
 * (active tab first, queued tabs afterwards).
 *
 * Run: node test/e2e/restore.js
 */

const path = require('path');
const os = require('os');
const fs = require('fs');
const { spawn } = require('child_process');
const puppeteer = require('puppeteer-core');

const ELECTRON = path.join(__dirname, '..', '..', 'node_modules', 'electron', 'dist', 'electron.exe');
const ROOT = path.join(__dirname, '..', '..');
const PORT = 9346;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function spawnApp(profileDir) {
  const proc = spawn(ELECTRON, ['.', `--remote-debugging-port=${PORT}`], {
    cwd: ROOT,
    stdio: ['ignore', 'pipe', 'pipe'],
    env: Object.assign({}, process.env, { WEB98_USER_DATA: profileDir })
  });
  let log = '';
  proc.stdout.on('data', (d) => { log += d; });
  proc.stderr.on('data', (d) => { log += d; });
  return { proc, getLog: () => log };
}

async function connect() {
  const deadline = Date.now() + 20000;
  for (;;) {
    try {
      const res = await fetch(`http://127.0.0.1:${PORT}/json/version`);
      if (res.ok) break;
    } catch (e) { /* retry */ }
    if (Date.now() > deadline) throw new Error('debugger did not start');
    await sleep(100);
  }
  const browser = await puppeteer.connect({ browserURL: `http://127.0.0.1:${PORT}`, defaultViewport: null });
  let chrome = null;
  const end = Date.now() + 15000;
  while (Date.now() < end && !chrome) {
    const pages = await browser.pages();
    chrome = pages.find((p) => p.url().includes('index.html'));
    if (!chrome) await sleep(150);
  }
  if (!chrome) throw new Error('chrome page not found');
  return { browser, chrome };
}

let failures = 0;
function check(name, ok, extra) {
  if (ok) console.log(`  PASS  ${name}`);
  else {
    failures++;
    console.log(`  FAIL  ${name}${extra ? ' — ' + extra : ''}`);
  }
}

async function main() {
  /* fresh shared profile for both runs; never touches the user's data */
  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'web98-restore-'));
  /* --- run 1: build a session and quit gracefully --- */
  const first = spawnApp(profileDir);
  let browser;
  try {
    await sleep(1500);
    ({ browser, chrome } = await connect());
    const local = 'file:///' + path.join(ROOT, 'src', 'renderer', 'startpage.html').replace(/\\/g, '/');
    for (let i = 0; i < 2; i++) {
      await chrome.evaluate(async () => { await window.web98.invoke('tabs:create', {}); });
      await sleep(300);
      await chrome.evaluate((u, n) => {
        const input = document.getElementById('address');
        input.value = u + '#' + n;
        input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
      }, local, i);
      await chrome.waitForFunction(() => {
        const el = document.getElementById('status');
        return el && el.textContent === 'Done';
      }, { timeout: 5000 });
    }
    const stripBefore = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    check('session has 3 tabs', stripBefore === 3, `got ${stripBefore}`);
    /* activate the middle tab so restore has a non-default active idx */
    await chrome.evaluate(() => {
      document.querySelectorAll('.ow-tab')[1].dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0 }));
    });
    await sleep(400);
    const activeIdx = await chrome.evaluate(() =>
      Array.from(document.querySelectorAll('.ow-tab')).findIndex((t) => t.classList.contains('is-active'))
    );
    check('middle tab activated', activeIdx === 1, `idx=${activeIdx}`);

    /* second window so the saved session spans two windows */
    await chrome.evaluate(() => window.web98.invoke('app:command', { name: 'file:new-window' }));
    let w2 = null;
    for (let i = 0; i < 40 && !w2; i++) {
      const idxPages = (await browser.pages()).filter((p) => p.url().includes('index.html'));
      w2 = idxPages.find((p) => p !== chrome) || null;
      if (!w2) await sleep(300);
    }
    check('second window opened', !!w2);
    if (w2) {
      /* wait until the second window's chrome script is live (tab strip
         rendered) and its homepage has committed, so the address-bar nav
         below is a same-document jump on a settled page */
      await w2.waitForFunction(() => document.querySelectorAll('.ow-tab').length >= 1, { timeout: 10000 });
      await w2.waitForFunction(async () => {
        const list = await window.web98.invoke('tabs:list');
        const t = list.find((x) => x.active);
        return !!(t && t.url && t.url.includes('startpage.html') && !t.loading);
      }, { timeout: 10000 });
      await w2.evaluate((u) => {
        const input = document.getElementById('address');
        input.value = u;
        input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
      }, local + '#w2');
      /* Done AND the address bar shows the committed url (not a stale Done) */
      await w2.waitForFunction(() => {
        const st = document.getElementById('status');
        const ad = document.getElementById('address');
        return st && st.textContent === 'Done' && ad && ad.value.includes('#w2');
      }, { timeout: 8000 });
    }

    /* graceful quit with BOTH windows open: before-quit must persist them */
    await chrome.evaluate(() => {
      setTimeout(() => { window.web98.invoke('app:command', { name: 'file:exit' }); }, 50);
    });
    const exitDeadline = Date.now() + 10000;
    while (first.proc.exitCode === null && Date.now() < exitDeadline) await sleep(100);
    check('app exited gracefully', first.proc.exitCode === 0, `code=${first.proc.exitCode}`);
  } finally {
    if (browser) { try { await browser.disconnect(); } catch (e) { /* ignore */ } }
    if (first.proc.exitCode === null) {
      try { require('child_process').execSync(`taskkill /F /PID ${first.proc.pid} /T`, { stdio: 'ignore' }); } catch (e) { /* ignore */ }
    }
  }
  await sleep(800);

  /* --- run 2: restore --- */
  const second = spawnApp(profileDir);
  try {
    await sleep(1500);
    const conn = await connect();
    browser = conn.browser;
    chrome = conn.chrome;

    /* both windows must come back */
    let indexPages = [];
    for (let i = 0; i < 40; i++) {
      indexPages = (await browser.pages()).filter((p) => p.url().includes('index.html'));
      if (indexPages.length >= 2) break;
      await sleep(400);
    }
    check('both windows restored', indexPages.length === 2, `got ${indexPages.length}`);

    /* deterministic window roles: 3 tabs (window 1) vs 1 tab (window 2) */
    const snap = async (p) => p.evaluate(async () => ({
      strip: document.querySelectorAll('.ow-tab').length,
      list: (await window.web98.invoke('tabs:list')).map((t) => ({ url: t.url, active: t.active }))
    }));
    let win3 = null;
    let win1 = null;
    for (let i = 0; i < 40 && (!win3 || !win1); i++) {
      for (const p of indexPages) {
        const s = await snap(p);
        if (s.strip === 3) win3 = s;
        else if (s.strip === 1) win1 = s;
      }
      if (!win3 || !win1) await sleep(400);
    }
    check('3-tab window restored instantly', !!win3,
      win3 ? JSON.stringify(win3) : 'never saw a 3-tab strip');
    check('second window restored with its tab', !!win1,
      win1 ? JSON.stringify(win1) : 'never saw a 1-tab strip');

    if (win3) {
      check('exactly one restored tab active', win3.list.filter((t) => t.active).length === 1,
        JSON.stringify(win3.list));
      const activeUrl = (win3.list.find((t) => t.active) || {}).url || '';
      check('restored active tab keeps its url', activeUrl.includes('startpage.html'), activeUrl);
    } else {
      check('exactly one restored tab active', false, 'no 3-tab window');
      check('restored active tab keeps its url', false, 'no 3-tab window');
    }
    if (win1) {
      const w2Url = (win1.list[0] || {}).url || '';
      check('second window kept its url', w2Url.includes('#w2'), w2Url);
    } else {
      check('second window kept its url', false, 'no 1-tab window');
    }

    /* background tabs load staggered: eventually every tab has a page target */
    let grown = false;
    for (let i = 0; i < 40; i++) {
      const pages = (await browser.pages()).filter((p) => p.url().includes('startpage.html'));
      if (pages.length >= 4) { grown = true; break; }
      await sleep(500);
    }
    check('background tabs load after restore', grown,
      (await browser.pages()).map((p) => p.url()).join(' | '));

    /* final state: both strips stable after all loads */
    let final3 = null;
    let final1 = null;
    for (let i = 0; i < 20 && (final3 == null || final1 == null); i++) {
      final3 = null;
      final1 = null;
      for (const p of indexPages) {
        const s = await snap(p);
        if (s.strip === 3) final3 = s;
        else if (s.strip === 1) final1 = s;
      }
      if (final3 == null || final1 == null) await sleep(400);
    }
    check('tab counts stable after restore loads', final3 != null && final1 != null,
      `3-tab=${final3} 1-tab=${final1}`);

    await browser.disconnect();
    browser = null;
    if (second.proc.exitCode === null) {
      try { require('child_process').execSync(`taskkill /F /PID ${second.proc.pid} /T`, { stdio: 'ignore' }); } catch (e) { /* ignore */ }
    }
  } finally {
    if (browser) { try { await browser.disconnect(); } catch (e) { /* ignore */ } }
    if (second.proc.exitCode === null) {
      try { require('child_process').execSync(`taskkill /F /PID ${second.proc.pid} /T`, { stdio: 'ignore' }); } catch (e) { /* ignore */ }
    }
  }

  try { fs.rmSync(profileDir, { recursive: true, force: true }); } catch (e) { /* ignore */ }
  console.log(failures ? `\n${failures} check(s) FAILED` : '\nALL RESTORE CHECKS PASSED');
  process.exit(failures ? 1 : 0);
}

main().catch((err) => {
  console.error('restore fatal:', err);
  process.exit(1);
});
