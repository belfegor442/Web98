# Web98

A real desktop web browser with an authentic Windows 98 / Internet Explorer 5
interface. It browses the live web — not a mockup: real navigation, real tabs,
real downloads.

Built with Electron (`WebContentsView` tabs), a custom retro chrome UI, and
`better-sqlite3` for history/bookmarks/settings.

## Features

- **Real browsing** — loads live websites in sandboxed, out-of-process tabs
  (`persist:web98` session partition).
- **Tabs** — create/close/switch, per-tab loading state, favicon + title,
  Ctrl+T / Ctrl+W / Ctrl+Tab accelerators.
- **Address bar** — URL vs. search disambiguation (`src/shared/urlbar.js`),
  autocomplete of history/favorites, dropdown panel, Go button, Links menu.
- **Navigation** — Back/Forward/Stop/Reload/Home toolbar buttons with real
  enabled/disabled states, Alt+Left/Right, status bar with progress + errors.
- **Favorites** — side panel with folders, add/edit/remove, Ctrl+D.
- **History** — SQLite-backed, searchable dialog (Ctrl+H), autocompletion source.
- **Downloads** — download manager with progress, cancel, "open when done"
  confirmation in a retro dialog, folder via shell.
- **Settings dialog** — homepage, search engine (Google/DuckDuckGo/Bing
  templates), new-tab behavior, font size; persisted to disk.
- **Find in page** — Ctrl+F find bar with next/prev/highlight/count, F3/Shift+F3.
- **Context menus** — page/link/image/editable menus rendered in the retro style,
  built per right-click from the main process.
- **Start page** — local `startpage.html` with a search box, favorites, and
  history shortcuts.
- **Menus** — full File/Edit/View/Favorites/Tools/Help menu bar; all commands go
  through one executor (`menus.js`), shared with accelerators and context menus.
- **Title bar** — frameless window with real Win98 focus behavior: blue when
  the window is focused, gray when another window is in front.
- **Session restore** — reopens your tabs from the previous run (first window).

## Security model

- **URL policy** — shared `src/shared/safeurl.js` decides what may load in a
  tab, what a web page may navigate to (no `file:` from remote pages), and
  what may leave the app via `shell.openExternal` (`mailto:`/`tel:` only).
  `javascript:` is never evaluated from the address bar.
- **IPC validation** — every handler in `src/main/ipc.js` validates its
  sender (chrome / dialog / tab) and its arguments (ids, sizes, URLs,
  command names); settings patches go through a strict whitelist
  (`src/shared/validate.js`).
- **Preload allowlists** — chrome/dialog preload exposes fixed
  invoke/send/on channel lists; the tab preload exposes only
  `web98cert.proceed(url)` (type-checked, main-process verified against the
  error page currently shown).
- **Sandboxed content** — tabs run with `sandbox: true`, `contextIsolation`,
  `webviewTag: false`.
- **Certificates** — invalid/unknown issuers are blocked by default and shown a
  retro error page with an explicit "continue anyway" action; the grant is
  scoped to that tab + URL and expires after 5 minutes.
- **Permissions** — camera/mic/geo/notifications denied by default; per-origin
  boolean grants persisted through a validated path only.
- **Chrome UI is local-only** — `will-navigate`/`window.open` blocked on the
  chrome webContents; dialogs block `window-open`/`will-navigate` too.
- **Downloads** — filenames sanitized (no path traversal, containment
  checked); no auto-execution; open-after-download requires confirmation.

## Quick start

```sh
npm start            # run the browser (Electron)
npm test             # 54 unit tests (runs inside Electron)
npm run test:e2e     # 26 end-to-end checks (puppeteer-core + CDP)
npm run test:restore # session-restore regression (2 app runs)
npm run bench        # startup / tab-switch / memory benchmark
npm run bench:memory # per-process memory/CPU + background-throttle benchmark
npm run dist         # Windows x64: NSIS installer + portable exe -> dist/
npm run dist:win-arm64  # Windows ARM64 installer + portable exe
npm run dist:linux   # Linux x64 unpacked app dir
npm run dist:linux-arm64 # Linux ARM64 unpacked app dir
```

Environment notes: Node 24 + Electron 44. `better-sqlite3` v13 is N-API and
loads its platform binary from `prebuilds/<platform>-<arch>.node`, so no
rebuild toolchain is needed (`npmRebuild: false` in the build config);
`npm run rebuild` remains available if you change ABI assumptions. Test and
benchmark suites launch the app with `WEB98_USER_DATA` pointed at a throwaway
profile, so they never read or write your real settings/history/session. Some
networks block arbitrary hosts — the e2e suite uses `wikipedia.org`.

## Architecture

```
src/
  main/            main process
    main.js        bootstrap, single-instance lock, accelerator wiring
    windows.js     frameless window, focus/blur -> win:active, dialogs
    tabs.js        WebContentsView per tab, bounds, visibility, nav state
    ipc.js         typed IPC surface (invoke/send), content-bounds push
    menus.js       menu templates, context menu builder, command executor
    store.js       SQLite: history, bookmarks, favorites, autocompletion
    settings.js    JSON settings + search-engine templates
    downloads.js   session download handling + progress events
    security.js    certificate blocking, permission policy
    preload.js     chrome renderer bridge (invoke/send/on)
    preload-tab.js minimal tab-page bridge (cert continue only)
  shared/
    urlbar.js      pure URL-vs-search classifier (unit-tested)
    safeurl.js     URL/navigation/external-protocol policy (unit-tested)
    validate.js    input validation: ids, filenames, bookmarks, settings
  renderer/
    index.html     chrome UI shell
    chrome.js      chrome controller: tabs, address, panels, dialogs, menus
    chrome.css     layout only (skin from assets/ie5 + css/ie5-shell.css)
    error.html     certificate / network error pages
    startpage.html local start page
    dialogs/       history, downloads, favorites, settings, about, source
assets/ie5/        authentic IE5/Win98 skin (theme.css, icons)
css/ie5-shell.css  win98 window/focus/toggle chrome wiring
test/              unit (Electron runner), e2e (puppeteer), bench
```

Data flow: renderer events go over a narrow IPC surface; navigation state,
tab lists, and content bounds are pushed from main (`tabs`, `nav`, `chrome:bounds`).
Floating UI (menus, panels) shrinks the content bounds so tabs reflow exactly
like a docked pane.

## Platform support

Built and verified on this machine (Windows 10/11, x64):

| Target                 | Build            | Runtime verified      |
| ---------------------- | ---------------- | --------------------- |
| Windows x64 (NSIS + portable) | Yes, signed | Packaged exe boots, SQLite DB created |
| Windows ARM64 (NSIS + portable) | Yes, signed | Not run (no ARM64 host here) |
| Linux x64 (unpacked app dir) | Yes            | Not run (no Linux host here) |
| Linux ARM64 (unpacked app dir) | Yes          | Not run (no Linux host here) |
| macOS (DMG)            | Not built — requires a macOS host (notarization also needs macOS) | No |
| Windows x86 / Linux ARMv7 | **Unavailable** — Electron 44 ships no `win32-ia32` or `linux-armv7l` builds | — |

`.deb` and `.AppImage` installers additionally need host tooling this Windows
machine does not have (`fpm` for deb; `mksquashfs` for AppImage — the bundled
tool is a macOS/Linux binary). On a Linux host, install `fpm` and run
`npx electron-builder --linux deb AppImage`; AppImage can also be built from
macOS. Linux/macOS packages were produced from the same sources and contain
the correct `better-sqlite3` prebuilds, but no Linux/macOS runtime testing has
been performed here — treat those targets as packaged-but-untested.

## Known limitations

- Session restore reopens the first window only; it is saved both on quit
  and when the last window is closed (closing the window used to drop it).
- No extension support (by design for this scope).
- Certificate "continue anyway" decisions apply to that tab + URL only and
  expire after 5 minutes (re-clicking renews them).
- Downloads go to the system downloads folder; the location is not yet
  user-configurable in Settings.
- A few confirmation prompts use the native `confirm`/`messageBox` instead of
  a retro dialog.
- The legacy browser-served Windows 98 prototype (`index.html`, `js/`, `css/`
  at the repo root) is kept intact as a visual reference; the Electron app
  reuses its skin assets.

## Testing

- `npm test` — pure logic (urlbar, safe URL policy, validation, store
  limits/cascade deletes, settings, menus/accelerators, downloads incl.
  finished-history cap and coalesced progress emits): **54/54 passing**.
- `npm run test:e2e` — full browser flows against the live web (tabs,
  accelerators, menus, Wikipedia navigation, panels, dialogs, find, context
  menu, `javascript:`/`file:` navigation refused, tab create/close releasing
  its renderer page, zero console errors): **26/26 passing**. The suite brings
  the app window to the foreground first — fully occluded windows drop
  dispatched input, which would flake pointer checks on machines running
  fullscreen games.
- `npm run test:restore` — quits the app gracefully with 3 tabs open and
  relaunches: session persisted, active tab index kept, background tabs
  deferred-loaded without racing: **8/8 passing**.
- `npm run bench` — measured on this machine, old code (commit `381ef77`)
  vs. optimized code, same session, 3 runs each (medians): debugger up
  154 → 181 ms, app ready 195 → 166 ms, chrome UI loaded 262 → 231 ms,
  UI settled 1271 → 1237 ms, idle RSS 99.8 → 99.0 MB, idle CPU ~1% → ~1%,
  load CPU ~42 → ~46%, tab switch median 5–6 → 5–6 ms, local nav 20–21 →
  20–23 ms, process count idle/5-tabs/closed 5/9/5 → 5/9/5. Differences are
  within run-to-run noise on this machine (Defender + editor contention);
  the structural wins are event-storm reductions (coalesced IPC pushes,
  no per-navigation DB prune, incremental tab-strip re-render, deferred
  background loads) and the regression suites that now pin them down.
  First cold run after touching all sources can spike (AV re-scan) — ignore
  outliers, compare warm runs. A later round (lazy background-tab views,
  explicit background timer throttling, coalesced download progress) measured
  medians of 3: debugger up 148 ms, app ready 138 ms, chrome loaded 191 ms,
  UI settled 1204 ms, idle RSS ~101 MB, idle CPU 0–1%, load CPU ~47%,
  tab switch median 12 ms (max 15), local nav 23 ms, procs 5/9/5.
- `npm run bench:memory` — per-process memory/CPU breakdown (role-labelled
  via Win32 process query), scenarios: idle, five simple pages, hidden-tab
  timer throttling, tab-churn leak check, final idle; hard checks + JSON
  output in `test/bench/memory-results.json` (gitignored). Machine: Ryzen 5
  7600X ×12, 31 GB RAM, Windows 10/11 build 26200, warm persistent profile,
  3 s settle, median of 3 runs after the optimization round: idle 5 procs
  **403 MB WS / 209 MB private, CPU 0–1.5%**; five pages 10 procs **845 MB
  WS / 349 MB private, CPU ~0.3%** (all 5 loaded); churn returns to 5 procs
  with flat working set across 3 rounds (~419–423 MB); hidden-tab ticker
  clamped to ~1.5 ticks/s (6 per 4 s) with hidden CPU 0–0.6%. The
  pre-optimization baseline (same bench, before lazy views / throttling
  changes) measured idle 421 MB WS / 233 MB private, five pages 871 / 357
  MB, and **failed** the throttle check — hidden timer kept running at
  ~17 ticks/s (67 per 4 s, no throttling at all while the window stayed
  visible). Methodology: kill stray processes first, fresh spawn per run,
  medians because GPU/Defender contention makes single runs noisy.
- `npm run dist` / `dist:win-arm64` — build `dist/Web98 Setup <version>
  <arch>.exe` (NSIS installer) and `dist/Web98 <version> <arch>.exe`
  (portable), signed; x64 verified to boot, open the start page, and create
  its SQLite database.
