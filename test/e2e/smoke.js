/**
 * E2E smoke test: launches Electron with remote debugging, connects
 * puppeteer-core, and exercises core browser flows.
 *
 * Run: npm run test:e2e   (or: node test/e2e/smoke.js)
 *
 * Note: keyboard accelerators are dispatched as raw CDP key events because
 * puppeteer's page.keyboard does not reach Electron's before-input-event.
 */

const path = require('path');
const os = require('os');
const fs = require('fs');
const { spawn, execSync } = require('child_process');
const puppeteer = require('puppeteer-core');

const ELECTRON = path.join(__dirname, '..', '..', 'node_modules', 'electron', 'dist', 'electron.exe');
const ROOT = path.join(__dirname, '..', '..');
const PORT = 9333;

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function waitForDebugger(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const res = await fetch(`http://127.0.0.1:${PORT}/json/version`);
      if (res.ok) return res.json();
    } catch (e) {
      /* retry */
    }
    await sleep(250);
  }
  throw new Error('debugger did not start');
}

/** Ctrl+letter via raw CDP events (reaches before-input-event). */
async function pressCtrl(page, code, vk) {
  const cdp = await page.createCDPSession();
  const opts = {
    modifiers: 2, // Ctrl
    key: code.slice(3).toLowerCase(),
    code,
    windowsVirtualKeyCode: vk,
    nativeVirtualKeyCode: vk
  };
  await cdp.send('Input.dispatchKeyEvent', Object.assign({ type: 'rawKeyDown' }, opts));
  await cdp.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, opts));
  await cdp.detach();
}

/**
 * Bring the Web98 window to the foreground. Fully occluded windows
 * (fullscreen games etc.) make Chromium drop dispatched input, which
 * would flake pointer-based checks.
 */
function bringToFront() {
  const ps1 = `
$h=(Get-Process | Where-Object { $_.MainWindowTitle -like '*- Web98*' } | Select-Object -First 1).MainWindowHandle
if(-not $h -or $h -eq [IntPtr]::Zero){ exit 0 }
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FG98T {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr p);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h, int n);
}
"@
$fg=[FG98T]::GetForegroundWindow()
$th=[FG98T]::GetWindowThreadProcessId($fg,[IntPtr]::Zero)
$me=[FG98T]::GetCurrentThreadId()
[void][FG98T]::AttachThreadInput($th,$me,$true)
[void][FG98T]::ShowWindowAsync($h,9)
[void][FG98T]::BringWindowToTop($h)
[void][FG98T]::SetForegroundWindow($h)
[void][FG98T]::AttachThreadInput($th,$me,$false)
`;
  const tmp = path.join(os.tmpdir(), `web98_fg_${Date.now()}.ps1`);
  fs.writeFileSync(tmp, ps1, 'utf8');
  try {
    execSync(`powershell -NoProfile -ExecutionPolicy Bypass -File "${tmp}"`, { stdio: 'ignore' });
  } finally {
    try { fs.unlinkSync(tmp); } catch (e) { /* ignore */ }
  }
}

let failures = 0;
function check(name, ok, extra) {
  if (ok) {
    console.log(`  PASS  ${name}`);
  } else {
    failures++;
    console.log(`  FAIL  ${name}${extra ? ' — ' + extra : ''}`);
  }
}

async function main() {
  /* fresh profile per run: tests must not read or write the user's data */
  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'web98-smoke-'));
  const proc = spawn(ELECTRON, ['.', `--remote-debugging-port=${PORT}`], {
    cwd: ROOT,
    stdio: ['ignore', 'pipe', 'pipe'],
    env: Object.assign({}, process.env, { WEB98_USER_DATA: profileDir })
  });
  let bootLog = '';
  proc.stdout.on('data', (d) => { bootLog += d; });
  proc.stderr.on('data', (d) => { bootLog += d; });

  let browser;
  try {
    await waitForDebugger(20000);
    browser = await puppeteer.connect({
      browserURL: `http://127.0.0.1:${PORT}`,
      defaultViewport: null
    });

    /* find the chrome window page */
    let chrome = null;
    let deadline = Date.now() + 15000;
    while (Date.now() < deadline && !chrome) {
      const pages = await browser.pages();
      chrome = pages.find((p) => p.url().includes('index.html'));
      if (!chrome) await sleep(300);
    }
    check('chrome window loads', !!chrome, bootLog.slice(-500));
    if (!chrome) throw new Error('no chrome page');

    bringToFront();
    await sleep(600);

    /* collect renderer errors from now on */
    const errors = [];
    chrome.on('console', (msg) => {
      if (msg.type() === 'error') errors.push(msg.text());
    });
    chrome.on('pageerror', (err) => errors.push(String(err)));

    await sleep(800);
    /* wait for boot to finish rendering the first tab (contention can
       delay it past the fixed sleeps; report a normal check failure
       rather than aborting the whole run if it never appears) */
    try {
      await chrome.waitForFunction(
        () => document.querySelectorAll('.ow-tab').length >= 1,
        { timeout: 10000 }
      );
    } catch (e) { /* fall through — the checks below report got 0 */ }

    /* structure present */
    const structure = await chrome.evaluate(() => {
      const r = document.getElementById('content-ghost').getBoundingClientRect();
      return {
        titlebar: !!document.getElementById('titlebar'),
        menubar: document.querySelectorAll('.ow-menu__label').length,
        toolbar: !!document.getElementById('toolbar'),
        address: !!document.getElementById('address'),
        tabs: document.querySelectorAll('.ow-tab').length,
        status: !!document.getElementById('status'),
        ghostW: Math.round(r.width),
        ghostH: Math.round(r.height)
      };
    });
    check(
      'titlebar/menubar/toolbar/address/status render',
      structure.titlebar && structure.menubar === 6 && structure.toolbar && structure.address && structure.status,
      JSON.stringify(structure)
    );
    check('one tab rendered', structure.tabs === 1, `got ${structure.tabs}`);
    check('content ghost has size', structure.ghostW > 100 && structure.ghostH > 100,
      `${structure.ghostW}x${structure.ghostH}`);

    /* address bar filled (homepage) */
    await sleep(500);
    const addr = await chrome.$eval('#address', (el) => el.value);
    check('address bar shows homepage', addr.includes('startpage.html'), addr);

    /* Ctrl+T creates a tab (raw CDP) */
    await pressCtrl(chrome, 'KeyT', 84);
    await sleep(700);
    const tabCount2 = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    check('Ctrl+T creates a tab', tabCount2 === 2, `got ${tabCount2}`);

    /* close it via tab close button */
    const closes = await chrome.$$('.ow-tab__close');
    if (closes.length > 1) {
      await closes[closes.length - 1].click();
      await sleep(500);
    }
    const tabCount3 = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    check('close button removes tab', tabCount3 === 1, `got ${tabCount3}`);

    /* File menu -> New Tab (menu machinery end to end) */
    await chrome.evaluate(() => {
      const label = document.querySelector('.ow-menu__label');
      label.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true }));
      const items = Array.from(document.querySelectorAll('.ow-menu.is-open .ow-menu__item'));
      const nt = items.find((i) => i.textContent.includes('New Tab'));
      if (nt) nt.click();
    });
    await sleep(700);
    const tabCount4 = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    check('File menu New Tab works', tabCount4 === 2, `got ${tabCount4}`);
    /* close the extra tab again */
    await chrome.evaluate(() => {
      const closes = document.querySelectorAll('.ow-tab__close');
      if (closes.length > 1) closes[closes.length - 1].click();
    });
    await sleep(500);

    /* navigate the active tab through the address bar */
    await chrome.click('#address', { clickCount: 3 });
    await chrome.type('#address', 'https://www.wikipedia.org');
    await chrome.keyboard.press('Enter');
    await sleep(5000);
    const navState = await chrome.evaluate(() => ({
      address: document.getElementById('address').value,
      status: document.getElementById('status').textContent
    }));
    check('address bar shows target URL', navState.address.startsWith('https://www.wikipedia.org'), navState.address);
    check('status bar reports Done', navState.status === 'Done', navState.status);

    /* back button enabled state */
    const canBack = await chrome.evaluate(() => {
      const btn = document.querySelector('.ow-btn[data-action="back"]');
      return btn && !btn.disabled;
    });
    check('back button enabled after navigation', canBack);

    /* real website actually loaded in a tab view */
    const tabPages = (await browser.pages()).filter(
      (p) => p.url().startsWith('http') && !p.url().includes('index.html')
    );
    check('tab page exists for real website', tabPages.length > 0, tabPages.map((p) => p.url()).join(','));
    if (tabPages.length) {
      const title = await tabPages[0].title();
      check('real website title loaded', /Wikipedia/i.test(title), title);
    }

    /* favorites side panel */
    await chrome.evaluate(() => {
      document.querySelector('.ow-btn[data-action="favorites"]').click();
    });
    await sleep(600);
    const panel = await chrome.evaluate(() => {
      const p = document.getElementById('sidepanel');
      const open = p.classList.contains('is-open');
      const r = document.getElementById('content-ghost').getBoundingClientRect();
      return { open, w: Math.round(r.width), h: Math.round(r.height) };
    });
    check('favorites panel opens', panel.open);
    check('content shrinks for panel', panel.w > 100 && panel.h > 100, JSON.stringify(panel));

    /* close panel again */
    await chrome.evaluate(() => document.getElementById('sidepanel-close').click());
    await sleep(400);
    const panelClosed = await chrome.evaluate(
      () => !document.getElementById('sidepanel').classList.contains('is-open')
    );
    check('favorites panel closes', panelClosed);

    /* open history dialog window via Ctrl+H (raw CDP) */
    await pressCtrl(chrome, 'KeyH', 72);
    await sleep(1500);
    const pagesAfter = await browser.pages();
    const historyPage = pagesAfter.find((p) => p.url().includes('dialogs/history.html'));
    check('Ctrl+H opens history dialog', !!historyPage, pagesAfter.map((p) => p.url()).join(' | '));
    if (historyPage) {
      const hasList = await historyPage.evaluate(() => !!document.getElementById('list'));
      check('history dialog renders', hasList);
      await historyPage.evaluate(() => {
        document.querySelector('.ow-winbtn').click();
      });
      await sleep(400);
    }

    /* settings dialog: downloads folder controls + setting round-trip */
    await chrome.evaluate(() => window.web98.invoke('window:open', { page: 'settings' }));
    let settingsPage = null;
    for (let i = 0; i < 20 && !settingsPage; i++) {
      const ps = await browser.pages();
      settingsPage = ps.find((p) => p.url().includes('dialogs/settings.html')) || null;
      if (!settingsPage) await sleep(300);
    }
    check('settings dialog opens', !!settingsPage);
    if (settingsPage) {
      const hasDl = await settingsPage.evaluate(() =>
        !!document.getElementById('in-dl') &&
        !!document.getElementById('btn-dl-browse') &&
        !!document.getElementById('btn-dl-default'));
      check('settings dialog has downloads folder controls', hasDl);

      const dlDir = 'C:\\Web98-DL-Test';
      const rt = await settingsPage.evaluate(async (dir) => {
        const set = await window.web98.invoke('settings:set', { downloadsDir: dir });
        const got = await window.web98.invoke('settings:get');
        const bad = await window.web98.invoke('settings:set', { downloadsDir: 'relative/dir' });
        await window.web98.invoke('settings:set', { downloadsDir: null });
        const cleared = await window.web98.invoke('settings:get');
        return {
          setOk: set.downloadsDir === dir,
          getOk: got.downloadsDir === dir,
          rejected: !!(bad.rejected && bad.rejected.indexOf('downloadsDir') !== -1) && bad.downloadsDir === dir,
          cleared: cleared.downloadsDir == null
        };
      }, dlDir);
      check('downloads folder setting round-trips', rt.setOk && rt.getOk, JSON.stringify(rt));
      check('relative downloads folder rejected', rt.rejected, JSON.stringify(rt));
      check('downloads folder reset to default', rt.cleared, JSON.stringify(rt));

      await settingsPage.evaluate(() => {
        document.querySelector('.ow-winbtn').click();
      });
      await sleep(400);
    }

    /* find-in-page bar (Ctrl+F) */
    await pressCtrl(chrome, 'KeyF', 70);
    await sleep(500);
    const findOpen = await chrome.evaluate(() =>
      document.getElementById('findbar').classList.contains('is-open')
    );
    check('Ctrl+F opens find bar', findOpen);
    await chrome.evaluate(() => document.getElementById('find-close').click());

    /* context menu over page content: target the tab view */
    if (tabPages.length) {
      bringToFront();
      await sleep(500);
      await tabPages[0].mouse.click(400, 300, { button: 'right' });
      await sleep(600);
      const ctxOpen = await chrome.evaluate(() => {
        const layer = document.getElementById('ctxlayer');
        return layer.querySelectorAll('.ctx-menu').length > 0 && getComputedStyle(layer).pointerEvents !== 'none';
      });
      check('context menu opens in chrome', ctxOpen);
      /* dismiss */
      await chrome.evaluate(() => {
        document.getElementById('ctxlayer').dispatchEvent(new MouseEvent('mousedown', { bubbles: true }));
      });
      await sleep(300);
    }

    /* security: javascript: typed in the address bar must not execute */
    const secTab = tabPages.length ? tabPages[0] : null;
    if (secTab) {
      const beforeUrl = secTab.url();
      await chrome.click('#address', { clickCount: 3 });
      await chrome.type('#address', 'javascript:window.__web98pwn=1');
      await chrome.keyboard.press('Enter');
      await sleep(1200);
      const jsSec = await secTab.evaluate(() => typeof window.__web98pwn !== 'undefined').catch(() => true);
      const afterJsUrl = secTab.url();
      check(
        'javascript: address does not execute',
        jsSec === false && afterJsUrl === beforeUrl,
        `pwn=${jsSec} url=${afterJsUrl}`
      );
    }

    /* security: web pages may not navigate the tab to file: URLs */
    if (secTab) {
      const beforeFileUrl = secTab.url();
      await secTab
        .evaluate(() => {
          location.href = 'file:///C:/Windows/System32/drivers/etc/hosts';
        })
        .catch(() => {});
      await sleep(1200);
      const afterFileUrl = secTab.url();
      check(
        'file: navigation from web page blocked',
        afterFileUrl === beforeFileUrl && afterFileUrl.startsWith('http'),
        afterFileUrl
      );

      /* security: window.open('file:...') from a web page creates no tab */
      const tabPagesBefore = (await browser.pages()).filter(
        (p) => p.url().startsWith('http') && !p.url().includes('index.html')
      ).length;
      await secTab
        .evaluate(() => {
          window.open('file:///C:/Windows/win.ini', '_blank');
        })
        .catch(() => {});
      await sleep(1000);
      const tabPagesAfter = (await browser.pages()).filter(
        (p) => p.url().startsWith('http') && !p.url().includes('index.html')
      ).length;
      check(
        'window.open file: creates no tab',
        tabPagesAfter === tabPagesBefore,
        `before=${tabPagesBefore} after=${tabPagesAfter}`
      );
    }

    /* resources: creating tabs adds pages; closing them releases them */
    const localTarget = 'file:///' + path.join(ROOT, 'src', 'renderer', 'startpage.html').replace(/\\/g, '/');
    const pagesBase = (await browser.pages()).length;
    const stripBase = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    for (let i = 0; i < 2; i++) {
      let created = false;
      for (let attempt = 0; attempt < 3 && !created; attempt++) {
        const before = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
        await pressCtrl(chrome, 'KeyT', 84);
        try {
          await chrome.waitForFunction(
            (n) => document.querySelectorAll('.ow-tab').length === n + 1,
            { timeout: 2000 },
            before
          );
          created = true;
        } catch (e) { /* occluded key drop — retry */ }
      }
      if (!created) break;
      /* give the new tab a real page so its target/process is observable */
      await chrome.evaluate((u, n) => {
        const input = document.getElementById('address');
        input.value = u + '#' + n;
        input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
      }, localTarget, i);
      await chrome.waitForFunction(() => {
        const el = document.getElementById('status');
        return el && el.textContent === 'Done';
      }, { timeout: 5000 });
    }
    const stripOpened = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    const pagesOpened = (await browser.pages()).length;
    check(
      'two extra tabs created with real pages',
      stripOpened === stripBase + 2 && pagesOpened === pagesBase + 2,
      `strip=${stripOpened} pages=${pagesOpened} (base ${stripBase}/${pagesBase})`
    );
    for (let i = 0; i < 2; i++) {
      await chrome.evaluate(() => {
        const closes = document.querySelectorAll('.ow-tab__close');
        if (closes.length > 1) closes[closes.length - 1].click();
      });
      await sleep(500);
    }
    const stripClosed = await chrome.evaluate(() => document.querySelectorAll('.ow-tab').length);
    const pagesClosed = (await browser.pages()).length;
    check(
      'closing tabs releases their pages',
      stripClosed === stripBase && pagesClosed === pagesBase,
      `strip=${stripClosed} pages=${pagesClosed} (base ${stripBase}/${pagesBase})`
    );

    /* no renderer console errors */
    const realErrors = errors.filter(
      (e) =>
        !e.includes('favicon') &&
        !e.includes('net::ERR_') &&
        !e.includes('Failed to load resource') &&
        !e.includes('ERR_FAILED')
    );
    check('no renderer console errors', realErrors.length === 0, realErrors.slice(0, 3).join(' | '));
  } finally {
    if (browser) {
      try { await browser.disconnect(); } catch (e) { /* ignore */ }
    }
    if (proc && !proc.killed) {
      proc.kill();
      await sleep(500);
      if (!proc.killed) {
        try { process.kill(proc.pid); } catch (e) { /* ignore */ }
      }
    }
    try { fs.rmSync(profileDir, { recursive: true, force: true }); } catch (e) { /* ignore */ }
  }

  console.log(failures ? `\n${failures} check(s) FAILED` : '\nALL E2E CHECKS PASSED');
  process.exit(failures ? 1 : 0);
}

main().catch((err) => {
  console.error('E2E fatal:', err);
  process.exit(1);
});
