'use strict';

/**
 * IPC surface between the retro chrome UI (and dialog windows) and the
 * browser core. Every handler validates its sender context where needed.
 */

var fs = require('fs');
var path = require('path');
var { pathToFileURL } = require('url');
var { ipcMain, dialog, clipboard, app } = require('electron');
var { parseAddress } = require('../shared/urlbar');
var safeurl = require('../shared/safeurl');
var validate = require('../shared/validate');
var menus = require('./menus');

var COMMAND_NAME_RE = /^[a-z][a-z0-9\-]*:[a-z0-9\-]+$/;
var CONTEXT_COMMAND_RE = /^[a-z][a-z0-9\-]*$/;
var DIALOG_PAGES = ['about', 'downloads', 'favorites', 'history', 'settings', 'source'];
var ERROR_PAGE = path.join(__dirname, '..', 'renderer', 'error.html');

function registerIpc(deps) {
  // deps: { settings, store, downloads, security, wm, tabsOf, broadcast, searchTemplate }
  var settings = deps.settings;
  var store = deps.store;

  function stateOf(event) {
    return deps.wm.byChrome(event.sender);
  }
  function isChromeSender(event) {
    return !!stateOf(event);
  }
  function isDialogSender(event) {
    return !!deps.wm.isDialogWc(event.sender);
  }
  function isTabSender(event) {
    return !!deps.wm.findTabByWc(event.sender);
  }

  // Start-page actions are accepted only from the bundled local start page.
  ipcMain.handle('startpage:action', function (e, data) {
    var startUrl = pathToFileURL(path.join(__dirname, '..', 'renderer', 'startpage.html')).href;
    if (!isTabSender(e) || e.sender.getURL() !== startUrl) throw new Error('Start page action denied');
    var action = data && data.action;
    if (action === 'search') {
      var query = validate.sanitizeText(data && data.query, 500);
      if (!query) return { ok: false };
      var parsed = parseAddress(query, { searchTemplate: settings.searchTemplate() });
      if (parsed.type !== 'url' && parsed.type !== 'search') return { ok: false };
      if (!safeurl.isLoadableUrl(parsed.url)) return { ok: false };
      var found = deps.wm.findTabByWc(e.sender);
      var tab = found && found.tab;
      if (!tab || !tab.wc || tab.wc.isDestroyed()) return { ok: false };
      tab.wc.loadURL(parsed.url);
      return { ok: true };
    }
    if (['favorites', 'history', 'settings', 'about'].indexOf(action) === -1) throw new Error('Unknown start page action');
    var owner = e.sender.getOwnerBrowserWindow();
    if (!owner) return { ok: false };
    deps.wm.openDialog(action, null, owner);
    return { ok: true };
  });
  function tabsOf(event) {
    var s = stateOf(event);
    return s ? s.tabs : null;
  }
  function requireTabs(event) {
    var t = tabsOf(event);
    if (!t) throw new Error('No browser window for this request');
    return t;
  }
  function requireChrome(event) {
    var s = stateOf(event);
    if (!s) throw new Error('Browser chrome required');
    return s;
  }

  /* ------------------------------ window ------------------------------- */
  ipcMain.handle('win:min', function (e) {
    var s = stateOf(e);
    if (s) s.win.minimize();
  });
  ipcMain.handle('win:max', function (e) {
    var s = stateOf(e);
    if (!s) return false;
    if (s.win.isMaximized()) s.win.unmaximize();
    else s.win.maximize();
    return s.win.isMaximized();
  });
  ipcMain.handle('win:close', function (e) {
    var s = stateOf(e);
    if (s) s.win.close();
    else {
      var w = e.sender.getOwnerBrowserWindow();
      if (w) w.close();
    }
  });
  ipcMain.handle('win:title', function (e, title) {
    var s = stateOf(e);
    if (s) s.win.setTitle(validate.sanitizeText(title, 200) || 'Web98');
  });
  ipcMain.handle('win:isMax', function (e) {
    var s = stateOf(e);
    return s ? s.win.isMaximized() : false;
  });

  /* -------------------------------- tabs ------------------------------- */
  ipcMain.handle('tabs:list', function (e) {
    return requireTabs(e).list();
  });
  ipcMain.handle('tabs:create', function (e, data) {
    var url = data && data.url;
    if (url != null && (typeof url !== 'string' || !safeurl.isLoadableUrl(url))) {
      throw new Error('Refusing to load URL');
    }
    return requireTabs(e).create(url || null, true).id;
  });
  ipcMain.handle('tabs:close', function (e, data) {
    var id = validate.sanitizeId(data && data.id);
    if (id != null) requireTabs(e).close(id);
  });
  ipcMain.handle('tabs:activate', function (e, data) {
    var id = validate.sanitizeId(data && data.id);
    if (id != null) requireTabs(e).setActive(id);
  });

  /* ----------------------------- navigation ---------------------------- */
  ipcMain.handle('nav:go', function (e, data) {
    var tabs = requireTabs(e);
    var address = typeof (data && data.address) === 'string' ? data.address.slice(0, safeurl.MAX_URL_LEN) : '';
    var parsed = parseAddress(address, {
      searchTemplate: settings.searchTemplate()
    });
    if (parsed.type === 'empty') return { ok: false };
    if (parsed.type === 'javascript') {
      // Never evaluate page scripts from the address bar.
      return { ok: false, blocked: true };
    }
    if (!safeurl.isLoadableUrl(parsed.url)) return { ok: false, blocked: true };
    tabs.load(tabs.active(), parsed.url);
    return { ok: true, url: parsed.url };
  });
  ipcMain.handle('nav:back', function (e) {
    var t = requireTabs(e).active();
    if (t) t.wc.navigationHistory.goBack();
  });
  ipcMain.handle('nav:forward', function (e) {
    var t = requireTabs(e).active();
    if (t) t.wc.navigationHistory.goForward();
  });
  ipcMain.handle('nav:reload', function (e) {
    var t = requireTabs(e).active();
    if (t) t.wc.reload();
  });
  ipcMain.handle('nav:stop', function (e) {
    var t = requireTabs(e).active();
    if (t) t.wc.stop();
  });
  ipcMain.handle('nav:home', function (e) {
    var tabs = requireTabs(e);
    tabs.load(tabs.active(), settings.get('homepage'));
  });
  ipcMain.handle('app:find', function (e, data) {
    var t = requireTabs(e).active();
    if (!t) return;
    var action = data && data.action;
    var term = validate.sanitizeText(data && data.term, 200);
    if (action === 'stop') {
      t.wc.stopFindInPage('clearSelection');
    } else if (action === 'start' && term) {
      t.wc.findInPage(term, { forward: true, findNext: true });
    } else if (action === 'next' && term) {
      t.wc.findInPage(term, { forward: true, findNext: true });
    } else if (action === 'prev' && term) {
      t.wc.findInPage(term, { forward: false, findNext: true });
    }
  });

  /* ------------------------------ chrome ------------------------------- */
  ipcMain.handle('chrome:bounds', function (e, rect) {
    var s = stateOf(e);
    if (!s || !rect) return;
    if (!Number.isFinite(Number(rect.width)) || !Number.isFinite(Number(rect.height))) return;
    s.tabs.setBounds({
      x: validate.clampNumber(rect.x, -10000, 10000, 0),
      y: validate.clampNumber(rect.y, -10000, 10000, 0),
      width: validate.clampNumber(rect.width, 0, 10000, 0),
      height: validate.clampNumber(rect.height, 0, 10000, 0)
    });
  });
  ipcMain.on('chrome:popup', function (e, data) {
    var s = stateOf(e);
    if (s) s.tabs.setPopupHidden(!!(data && data.open));
  });
  ipcMain.on('ctx:command', function (e, data) {
    var s = stateOf(e);
    if (!s) return;
    var id = data && typeof data.id === 'string' && CONTEXT_COMMAND_RE.test(data.id) ? data.id : null;
    if (!id) return;
    var tab = s.tabs.active();
    if (!tab || !tab.lastCtx) return;
    var params = tab.lastCtx.params;
    tab.lastCtx = null;
    runContextCommand(id, { params: params }, s, deps);
  });

  /* ------------------------------ omni box ----------------------------- */
  ipcMain.handle('omni:suggest', function (e, data) {
    var q = validate.sanitizeText(data && data.q, 200).toLowerCase();
    if (!q) return [];
    var out = [];
    var seen = {};
    store.listBookmarks().forEach(function (b) {
      if (!b.url) return;
      var name = (b.name || '').toLowerCase();
      var url = b.url.toLowerCase();
      if (name.indexOf(q) !== -1 || url.indexOf(q) !== -1) {
        if (!seen[b.url]) {
          seen[b.url] = 1;
          out.push({ type: 'bookmark', title: b.name, url: b.url });
        }
      }
    });
    store.history({ q: q, limit: 8 }).forEach(function (h) {
      if (!seen[h.url] && out.length < 8) {
        seen[h.url] = 1;
        out.push({ type: 'history', title: h.title || h.url, url: h.url });
      }
    });
    return out.slice(0, 8);
  });

  /* ----------------------------- bookmarks ----------------------------- */
  function changedBooks() {
    deps.broadcast('bookmarks:changed');
  }
  ipcMain.handle('bookmarks:list', function () {
    return store.listBookmarks();
  });
  ipcMain.handle('bookmarks:add', function (e, data) {
    var b = validate.sanitizeBookmark(data);
    if (!b) return null;
    var created = store.addBookmark(b);
    changedBooks();
    return created;
  });
  ipcMain.handle('bookmarks:update', function (e, data) {
    var id = validate.sanitizeId(data && data.id);
    if (id == null) return null;
    var patch = validate.sanitizeBookmark({ name: data.name, url: data.url, parent: 0 });
    if (!patch) return null;
    var b = store.updateBookmark(id, { name: patch.name, url: patch.url });
    changedBooks();
    return b;
  });
  ipcMain.handle('bookmarks:remove', function (e, data) {
    var id = validate.sanitizeId(data && data.id);
    if (id == null) return;
    store.deleteBookmark(id);
    changedBooks();
  });
  ipcMain.handle('bookmarks:export', function (e) {
    var win = senderWindow(e);
    var res = dialog.showSaveDialogSync(win, {
      title: 'Export Favorites',
      defaultPath: 'web98-favorites.html.json',
      filters: [{ name: 'JSON', extensions: ['json'] }]
    });
    if (!res) return { ok: false };
    try {
      fs.writeFileSync(res, JSON.stringify({ bookmarks: store.listBookmarks() }, null, 2), 'utf8');
      return { ok: true, path: res };
    } catch (err) {
      return { ok: false, error: String(err.message || err) };
    }
  });
  ipcMain.handle('bookmarks:import', function (e) {
    var win = senderWindow(e);
    var res = dialog.showOpenDialogSync(win, {
      title: 'Import Favorites',
      filters: [{ name: 'JSON', extensions: ['json'] }]
    });
    if (!res || !res.length) return { ok: false };
    try {
      var data = JSON.parse(fs.readFileSync(res[0], 'utf8'));
      var bookmarks = validate.sanitizeBookmarkList(data && data.bookmarks);
      if (!bookmarks.length && !(Array.isArray(data && data.bookmarks) && data.bookmarks.length === 0)) {
        throw new Error('bad format');
      }
      bookmarks.forEach(function (b) {
        store.addBookmark(b);
      });
      changedBooks();
      return { ok: true, count: bookmarks.length };
    } catch (err) {
      return { ok: false, error: String(err.message || err) };
    }
  });

  /* ------------------------------ history ------------------------------ */
  ipcMain.handle('history:list', function (e, data) {
    var opts = validate.isPlainObject(data) ? data : {};
    var q = typeof opts.q === 'string' ? opts.q.slice(0, 200) : undefined;
    var limit = Number.isInteger(opts.limit) ? Math.min(Math.max(opts.limit, 1), 500) : 200;
    return store.history({ q: q, limit: limit });
  });
  ipcMain.handle('history:delete', function (e, data) {
    var id = validate.sanitizeId(data && data.id);
    if (id == null) return;
    store.deleteHistory(id);
    deps.broadcast('history:changed');
  });
  ipcMain.handle('history:clear', function () {
    store.clearHistory();
    deps.broadcast('history:changed');
  });

  /* ----------------------------- downloads ----------------------------- */
  function downloadId(d) {
    return validate.sanitizeId(d && d.id);
  }
  ipcMain.handle('downloads:list', function () {
    return deps.downloads.list();
  });
  ipcMain.handle('downloads:pause', function (e, d) { var id = downloadId(d); if (id != null) deps.downloads.pause(id); });
  ipcMain.handle('downloads:resume', function (e, d) { var id = downloadId(d); if (id != null) deps.downloads.resume(id); });
  ipcMain.handle('downloads:cancel', function (e, d) { var id = downloadId(d); if (id != null) deps.downloads.cancel(id); });
  ipcMain.handle('downloads:remove', function (e, d) { var id = downloadId(d); if (id != null) deps.downloads.remove(id); });
  ipcMain.handle('downloads:clearFinished', function () { deps.downloads.clearFinished(); });
  ipcMain.handle('downloads:open', function (e, d) { var id = downloadId(d); return id != null ? deps.downloads.openFile(id) : { ok: false }; });
  ipcMain.handle('downloads:showFolder', function (e, d) { var id = downloadId(d); return id != null ? deps.downloads.showFolder(id) : { ok: false }; });

  /* ------------------------------ settings ----------------------------- */
  ipcMain.handle('settings:get', function () {
    return settings.get();
  });
  ipcMain.handle('settings:set', function (e, patch) {
    var result = validate.sanitizeSettingsPatch(patch);
    Object.keys(result.patch).forEach(function (k) {
      settings.set(k, result.patch[k]);
    });
    var out = settings.get();
    if (result.rejected.length) out.rejected = result.rejected;
    return out;
  });
  ipcMain.handle('settings:pickDownloadsDir', function (e) {
    if (!isChromeSender(e) && !isDialogSender(e)) return null;
    var res = dialog.showOpenDialogSync(senderWindow(e), {
      title: 'Choose Downloads Folder',
      properties: ['openDirectory', 'createDirectory']
    });
    if (!res || !res.length) return null;
    var dir = res[0];
    if (!path.isAbsolute(dir) || dir.indexOf('\0') !== -1) return null;
    return dir;
  });
  ipcMain.handle('settings:clearData', function (e, opts) {
    var options = validate.isPlainObject(opts) ? opts : {};
    var ses = require('electron').session.fromPartition('persist:web98');
    if (options.history) store.clearHistory();
    if (options.cookies) ses.clearStorageData({ storages: ['cookies'] });
    if (options.cache) ses.clearCache();
    deps.broadcast('history:changed');
    return { ok: true };
  });
  ipcMain.handle('settings:grants', function () {
    return deps.security.listGrants();
  });
  ipcMain.handle('settings:revoke', function (e, d) {
    if (d && validate.isGrantOrigin(d.origin) && validate.isPermissionName(d.permission)) {
      deps.security.setGrant(d.origin, d.permission, null);
    }
    return deps.security.listGrants();
  });

  /* ------------------------------ windows ------------------------------ */
  ipcMain.handle('window:open', function (e, data) {
    var page = data && data.page;
    if (typeof page !== 'string' || DIALOG_PAGES.indexOf(page) === -1) return null;
    var s = stateOf(e);
    // Return a serializable ack — openDialog gives back a BrowserWindow.
    return deps.wm.openDialog(page, null, s ? s.win : null) ? page : null;
  });
  ipcMain.handle('window:close', function (e) {
    var w = e.sender.getOwnerBrowserWindow();
    if (w) w.close();
  });
  ipcMain.handle('app:open-url', function (e, data) {
    if (!isChromeSender(e) && !isDialogSender(e)) return false;
    var url = data && data.url;
    if (typeof url !== 'string' || !safeurl.isLoadableUrl(url)) return false;
    var w = deps.wm.primary();
    if (!w) return false;
    w.tabs.load(w.tabs.active(), url);
    if (!w.win.isDestroyed()) w.win.focus();
    return true;
  });

  /* ------------------------------ commands ----------------------------- */
  ipcMain.handle('app:command', function (e, data) {
    var s = stateOf(e);
    if (!s) return false;
    var name = data && typeof data.name === 'string' && COMMAND_NAME_RE.test(data.name) ? data.name : null;
    if (!name) return false;
    var arg = typeof data.arg === 'string'
      ? data.arg.slice(0, 200)
      : (typeof data.arg === 'number' && Number.isFinite(data.arg) ? data.arg : undefined);
    try {
      return menus.runCommand(name, arg, {
        state: s,
        tabs: s.tabs,
        settings: settings,
        windows: deps.wm,
        store: store
      });
    } catch (err) {
      console.error('[web98] command failed:', name, err);
      return false;
    }
  });
  ipcMain.handle('app:view-source', function (e) {
    var s = stateOf(e);
    if (!s) return { html: '', url: '' };
    var tab = s.tabs.active();
    if (!tab) return { html: '', url: '' };
    return tab.wc
      .executeJavaScript('document.documentElement.outerHTML', true)
      .then(function (html) {
        return { html: html, url: tab.url };
      })
      .catch(function () {
        return { html: '<!-- unable to read document -->', url: tab.url };
      });
  });
  ipcMain.handle('app:stats', function () {
    var mem = process.memoryUsage();
    return {
      rssMB: +(mem.rss / 1048576).toFixed(1),
      heapMB: +(mem.heapUsed / 1048576).toFixed(1),
      uptimeS: +process.uptime().toFixed(1)
    };
  });

  /* --------------------------- certificate flow ------------------------ */
  ipcMain.on('cert:proceed', function (e, url) {
    if (!isTabSender(e)) return;
    var found = deps.wm.findTabByWc(e.sender);
    if (!found) return;
    var tab = found.tab;
    if (!tab.error || tab.error.kind !== 'cert') return;
    if (tab.url !== url) return;
    // Only the internal error page may request the one-shot grant, and it
    // must still be the page currently displayed in this tab (query string
    // carries the error details and is ignored for comparison).
    try {
      var current = tab.wc.getURL().replace(/[?#].*$/, '');
      if (current !== pathToFileURL(ERROR_PAGE).href) return;
    } catch (err) {
      return;
    }
    deps.security.grantCert(tab.wc.id, url);
    tab.error = null;
    // Re-navigate to the original URL: the tab currently displays the local
    // error page, so a plain reload() would just reload that page.
    try {
      var p = tab.wc.loadURL(url);
      if (p && typeof p.catch === 'function') p.catch(function () {});
    } catch (err) {
      /* did-fail-load shows the error page again */
    }
  });

  function senderWindow(e) {
    var w = e.sender.getOwnerBrowserWindow();
    return w && !w.isDestroyed() ? w : undefined;
  }
}

function runContextCommand(id, ctxRef, state, deps) {
  var tabs = state.tabs;
  var tab = tabs.active();
  var params = ctxRef.params;
  if (!tab) return;

  switch (id) {
    case 'back': tab.wc.navigationHistory.goBack(); break;
    case 'forward': tab.wc.navigationHistory.goForward(); break;
    case 'reload': tab.wc.reload(); break;
    case 'stop': tab.wc.stop(); break;
    case 'open-link-tab': tabs.create(params.linkURL, true); break;
    case 'copy-link': clipboard.writeText(params.linkURL); break;
    case 'save-link': tab.wc.downloadURL(params.linkURL); break;
    case 'save-image': tab.wc.downloadURL(params.srcURL); break;
    case 'cut': tab.wc.cut(); break;
    case 'copy': tab.wc.copy(); break;
    case 'paste': tab.wc.paste(); break;
    case 'delete': tab.wc.delete(); break;
    case 'select-all': tab.wc.selectAll(); break;
    case 'view-source': menus.viewSource(tab, { windows: deps.wm, state: state }); break;
    case 'save-page': menus.savePage(tab); break;
    case 'print': tab.wc.print({ silent: false, printBackground: true }); break;
    case 'inspect': tab.wc.openDevTools({ mode: 'detach' }); break;
  }
}

module.exports = { registerIpc: registerIpc };
