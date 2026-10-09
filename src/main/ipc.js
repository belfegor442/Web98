'use strict';

/**
 * IPC surface between the retro chrome UI (and dialog windows) and the
 * browser core. Every handler validates its sender context where needed.
 */

var fs = require('fs');
var path = require('path');
var { ipcMain, dialog, clipboard, app } = require('electron');
var { parseAddress } = require('../shared/urlbar');
var menus = require('./menus');

function registerIpc(deps) {
  // deps: { settings, store, downloads, security, wm, tabsOf, broadcast, searchTemplate }
  var settings = deps.settings;
  var store = deps.store;

  function stateOf(event) {
    return deps.wm.byChrome(event.sender);
  }
  function tabsOf(event) {
    var s = stateOf(event);
    return s ? s.tabs : null;
  }
  function requireTabs(event) {
    var t = tabsOf(event);
    if (!t) throw new Error('No browser window for this request');
    return t;
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
    if (s) s.win.setTitle(String(title || 'Web98'));
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
    return requireTabs(e).create((data && data.url) || null, true).id;
  });
  ipcMain.handle('tabs:close', function (e, data) {
    requireTabs(e).close(data.id);
  });
  ipcMain.handle('tabs:activate', function (e, data) {
    requireTabs(e).setActive(data.id);
  });

  /* ----------------------------- navigation ---------------------------- */
  ipcMain.handle('nav:go', function (e, data) {
    var tabs = requireTabs(e);
    var parsed = parseAddress(data && data.address, {
      searchTemplate: settings.searchTemplate()
    });
    if (parsed.type === 'empty') return { ok: false };
    if (parsed.type === 'javascript') {
      tabs.load(tabs.active(), 'javascript:' + parsed.url);
      return { ok: true };
    }
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
    if (data.action === 'stop') {
      t.wc.stopFindInPage('clearSelection');
    } else if (data.action === 'start' && data.term) {
      t.wc.findInPage(data.term, { forward: true, findNext: true });
    } else if (data.action === 'next') {
      t.wc.findInPage(data.term, { forward: true, findNext: true });
    } else if (data.action === 'prev') {
      t.wc.findInPage(data.term, { forward: false, findNext: true });
    }
  });

  /* ------------------------------ chrome ------------------------------- */
  ipcMain.handle('chrome:bounds', function (e, rect) {
    var s = stateOf(e);
    if (s && rect) s.tabs.setBounds(rect);
  });
  ipcMain.on('chrome:popup', function (e, data) {
    var s = stateOf(e);
    if (s) s.tabs.setPopupHidden(!!(data && data.open));
  });
  ipcMain.on('ctx:command', function (e, data) {
    var s = stateOf(e);
    if (!s) return;
    var tab = s.tabs.active();
    if (!tab || !tab.lastCtx) return;
    runContextCommand(data && data.id, { params: tab.lastCtx.params }, s, deps);
  });

  /* ------------------------------ omni box ----------------------------- */
  ipcMain.handle('omni:suggest', function (e, data) {
    var q = String((data && data.q) || '').trim();
    if (!q) return [];
    var like = '%' + q + '%';
    var out = [];
    var seen = {};
    store.listBookmarks().forEach(function (b) {
      if (!b.url) return;
      if (b.name.toLowerCase().indexOf(q.toLowerCase()) !== -1 || b.url.toLowerCase().indexOf(q.toLowerCase()) !== -1) {
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
    var b = store.addBookmark({
      name: data.name || data.url,
      url: data.url,
      parent: data.parent || 0
    });
    changedBooks();
    return b;
  });
  ipcMain.handle('bookmarks:update', function (e, data) {
    var b = store.updateBookmark(data.id, { name: data.name, url: data.url });
    changedBooks();
    return b;
  });
  ipcMain.handle('bookmarks:remove', function (e, data) {
    store.deleteBookmark(data.id);
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
    fs.writeFileSync(res, JSON.stringify({ bookmarks: store.listBookmarks() }, null, 2), 'utf8');
    return { ok: true, path: res };
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
      if (!Array.isArray(data.bookmarks)) throw new Error('bad format');
      data.bookmarks.forEach(function (b) {
        store.addBookmark({ name: b.name, url: b.url, parent: b.parent || 0 });
      });
      changedBooks();
      return { ok: true, count: data.bookmarks.length };
    } catch (err) {
      return { ok: false, error: String(err.message || err) };
    }
  });

  /* ------------------------------ history ------------------------------ */
  ipcMain.handle('history:list', function (e, data) {
    return store.history(data || {});
  });
  ipcMain.handle('history:delete', function (e, data) {
    store.deleteHistory(data.id);
    deps.broadcast('history:changed');
  });
  ipcMain.handle('history:clear', function () {
    store.clearHistory();
    deps.broadcast('history:changed');
  });

  /* ----------------------------- downloads ----------------------------- */
  ipcMain.handle('downloads:list', function () {
    return deps.downloads.list();
  });
  ipcMain.handle('downloads:pause', function (e, d) { deps.downloads.pause(d.id); });
  ipcMain.handle('downloads:resume', function (e, d) { deps.downloads.resume(d.id); });
  ipcMain.handle('downloads:cancel', function (e, d) { deps.downloads.cancel(d.id); });
  ipcMain.handle('downloads:remove', function (e, d) { deps.downloads.remove(d.id); });
  ipcMain.handle('downloads:clearFinished', function () { deps.downloads.clearFinished(); });
  ipcMain.handle('downloads:open', function (e, d) { return deps.downloads.openFile(d.id); });
  ipcMain.handle('downloads:showFolder', function (e, d) { return deps.downloads.showFolder(d.id); });

  /* ------------------------------ settings ----------------------------- */
  ipcMain.handle('settings:get', function () {
    return settings.get();
  });
  ipcMain.handle('settings:set', function (e, patch) {
    var allowed = [
      'homepage', 'searchEngine', 'sessionRestore', 'statusBar',
      'toolbarVisible', 'zoom', 'downloadsDir', 'engines'
    ];
    Object.keys(patch || {}).forEach(function (k) {
      if (allowed.indexOf(k) !== -1) settings.set(k, patch[k]);
    });
    deps.broadcast('settings:changed', settings.get());
    return settings.get();
  });
  ipcMain.handle('settings:clearData', function (e, opts) {
    var ses = require('electron').session.fromPartition('persist:web98');    if (opts.history) store.clearHistory();
    if (opts.cookies) ses.clearStorageData({ storages: ['cookies'] });
    if (opts.cache) ses.clearCache();
    deps.broadcast('history:changed');
    return { ok: true };
  });
  ipcMain.handle('settings:grants', function () {
    return deps.security.listGrants();
  });
  ipcMain.handle('settings:revoke', function (e, d) {
    deps.security.setGrant(d.origin, d.permission, null);
    return deps.security.listGrants();
  });

  /* ------------------------------ windows ------------------------------ */
  ipcMain.handle('window:open', function (e, data) {
    var s = stateOf(e);
    return deps.wm.openDialog(data.page, data.payload || null, s ? s.win : null);
  });
  ipcMain.handle('window:close', function (e) {
    var w = e.sender.getOwnerBrowserWindow();
    if (w) w.close();
  });
  ipcMain.handle('app:open-url', function (e, data) {
    var url = data && data.url;
    if (!url) return false;
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
    try {
      return menus.runCommand(data.name, data.arg, {
        state: s,
        tabs: s.tabs,
        settings: settings,
        windows: deps.wm,
        store: store
      });
    } catch (err) {
      console.error('[web98] command failed:', data && data.name, err);
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
    var found = deps.wm.findTabByWc(e.sender);
    if (!found) return;
    var tab = found.tab;
    if (!tab.error || tab.error.kind !== 'cert') return;
    if (tab.url !== url) return;
    deps.security.grantCert(tab.wc.id, url);
    tab.error = null;
    tab.wc.reload();
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
