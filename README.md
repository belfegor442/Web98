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
npm test             # 48 unit tests (runs inside Electron)
npm run test:e2e     # 24 end-to-end checks (puppeteer-core + CDP)
npm run bench        # startup / tab-switch / memory benchmark
npm run rebuild      # rebuild better-sqlite3 for the Electron ABI
npm run dist         # Windows installer (NSIS) + portable exe -> dist/
npm run dist:portable  # portable exe only
```

Environment notes: Node 24 + Electron 44; `better-sqlite3` must be rebuilt for
the Electron ABI (see `npm run rebuild`). Some networks block arbitrary hosts —
the e2e suite uses `wikipedia.org`.

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

| Platform        | Status                                      |
| --------------- | ------------------------------------------- |
| Windows 10/11   | Tested (primary target)                     |
| macOS           | Untested — expected to work, unverified     |
| Linux           | Untested — expected to work, unverified     |

## Known limitations

- Session restore reopens the first window only.
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

- `npm test` — pure logic (urlbar, safe URL policy, validation, store,
  settings, menus/accelerators): **48/48 passing**.
- `npm run test:e2e` — full browser flows against the live web (tabs,
  accelerators, menus, Wikipedia navigation, panels, dialogs, find, context
  menu, `javascript:`/`file:` navigation refused, zero console errors):
  **24/24 passing**. The suite brings the app
  window to the foreground first — fully occluded windows drop dispatched
  input, which would flake pointer checks on machines running fullscreen games.
- `npm run bench` — startup ~150 ms to ready, tab switch median ~3 ms,
  idle RSS ~100 MB.
- `npm run dist` — builds `dist/Web98 Setup <version>.exe` (NSIS installer)
  and `dist/Web98 <version>.exe` (portable); verified to boot, open the
  start page, create its SQLite database and load real websites.
