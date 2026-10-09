'use strict';

/**
 * Web98 main process: boot, session, persistence, window orchestration.
 */

var { app, session, Menu } = require('electron');
var path = require('path');
var { openStore } = require('./store');
var { createSettings } = require('./settings');
var { createTabManager } = require('./tabs');
var { createDownloads } = require('./downloads');
var { initSecurity } = require('./security');
var { createWindowManager } = require('./windows');
var { registerIpc } = require('./ipc');
var menus = require('./menus');

app.setName('Web98');

var store;
var settings;
var downloads;
var security;
var wm;
var startupAt = Date.now();

var gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
  app.quit();
} else {
  app.on('second-instance', function () {
    var primary = wm && wm.primary();
    if (primary) {
      if (primary.win.isMinimized()) primary.win.restore();
      primary.win.focus();
    }
  });

  app.whenReady().then(boot).catch(function (err) {
    console.error('[web98] boot failed:', err);
    app.exit(1);
  });
}

function boot() {
  // Present as Chrome-ish UA so real sites do not reject the browser.
  app.userAgentFallback = app.userAgentFallback
    .replace(/\sElectron\/\S+/g, '')
    .replace(/\sWeb98\/\S+/g, '');

  Menu.setApplicationMenu(null);

  store = openStore(path.join(app.getPath('userData'), 'web98.db'));
  settings = createSettings(store, {
    renderer: path.join(__dirname, '..', 'renderer')
  });

  var ses = session.fromPartition('persist:web98');

  function broadcast(channel, payload) {
    if (wm) wm.eachMain(function (s) { s.emitRenderer(channel, payload); });
  }

  wm = createWindowManager({
    settings: settings,
    onKey: null, // set below (needs wm reference)
    createTabs: function (win, state) {
      return createTabManager({
        win: win,
        getZoom: function () { return settings.get('zoom'); },
        emit: function (channel, payload) { state.emitRenderer(channel, payload); },
        onKey: function (input) {
          return menus.handleAccelerator(input, {
            tabs: state.tabs || { active: function () { return null; }, create: function () {}, close: function () {} },
            settings: settings,
            state: state,
            wm: wm,
            setZoom: function (z) {
              state.tabs.setZoom(z);
              settings.set('zoom', z);
            },
            zoomBy: function (delta) {
              var t = state.tabs.active();
              if (!t) return;
              var z = Math.min(2.5, Math.max(0.25, Math.round((t.zoom + delta) * 100) / 100));
              state.tabs.setZoom(z);
              settings.set('zoom', z);
            }
          });
        },
        hooks: {
          onCommitted: function (tab) {
            if (/^https?:/i.test(tab.url)) {
              store.addHistory(tab.url, tab.title || '', Date.now());
              store.pruneHistory(5000);
            }
          },
          onLastClosed: null
        }
      });
    },
    onWindowReady: function (state) {
      var ms = Date.now() - startupAt;
      console.log('[web98] ready in ' + ms + ' ms');
      state.emitRenderer('app:ready', { startupMs: ms });
    }
  });

  // Wire keyboard handling for chrome windows (needs wm + state).
  wm.createMainWindow = wrapCreateMainWindow(wm.createMainWindow);

  downloads = createDownloads({
    session: ses,
    downloadsDir: function () { return settings.get('downloadsDir'); },
    emit: function (channel, payload) { broadcast(channel, payload); },
    onCompleted: null
  });

  security = initSecurity({ settings: settings, session: ses });

  settings.onChange(function (key, value) {
    broadcast('settings:changed', settings.get());
  });

  registerIpc({
    settings: settings,
    store: store,
    downloads: downloads,
    security: security,
    wm: wm,
    broadcast: broadcast
  });

  /* ------------------------------- session ------------------------------ */
  var sessionData = settings.get('lastSession');
  var restore = settings.get('sessionRestore') && sessionData && sessionData.urls && sessionData.urls.length
    ? sessionData
    : null;
  wm.createMainWindow(restore, true);

  app.on('before-quit', function () {
    if (!settings.get('sessionRestore')) return;
    var primary = wm.primary();
    if (!primary) return;
    var list = primary.tabs.list();
    var urls = [];
    var active = 0;
    list.forEach(function (t, i) {
      if (t.url && t.url !== 'about:blank') {
        if (t.active) active = urls.length;
        urls.push(t.url);
      }
    });
    settings.set('lastSession', { urls: urls, active: active });
  });

  app.on('window-all-closed', function () {
    if (process.platform !== 'darwin') app.quit();
  });

  app.on('activate', function () {
    if (wm.primary() === null) wm.createMainWindow(null, true);
  });

  app.on('will-quit', function () {
    if (store) store.close();
  });
}

/**
 * Decorates the window factory so chrome windows receive global keyboard
 * accelerators bound to their own tab manager.
 */
function wrapCreateMainWindow(original) {
  return function (sessionData, show) {
    var state = original(sessionData, show);
    state.win.webContents.on('before-input-event', function (e, input) {
      var handled = menus.handleAccelerator(input, {
        tabs: state.tabs,
        settings: settings,
        state: state,
        wm: wm,
        setZoom: function (z) {
          state.tabs.setZoom(z);
          settings.set('zoom', z);
        },
        zoomBy: function (delta) {
          var t = state.tabs.active();
          if (!t) return;
          var z = Math.min(2.5, Math.max(0.25, Math.round((t.zoom + delta) * 100) / 100));
          state.tabs.setZoom(z);
          settings.set('zoom', z);
        }
      });
      if (handled) e.preventDefault();
    });
    return state;
  };
}
