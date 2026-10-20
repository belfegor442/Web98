'use strict';

/**
 * Window management: main browser windows (frameless, retro chrome) and
 * secondary dialog windows (favorites, history, downloads, settings, about,
 * source). Also handles session restore hooks.
 */

var { BrowserWindow } = require('electron');
var path = require('path');
var { sanitizeSessionList } = require('../shared/safeurl');

var RENDERER = path.join(__dirname, '..', 'renderer');
var PRELOAD = path.join(__dirname, 'preload.js');

var DIALOG_SPEC = {
  about: { w: 400, h: 240, title: 'About Web98' },
  source: { w: 720, h: 520, title: 'Source' },
  favorites: { w: 540, h: 440, title: 'Organize Favorites' },
  history: { w: 580, h: 470, title: 'History' },
  downloads: { w: 580, h: 420, title: 'Downloads' },
  passwords: { w: 520, h: 400, title: 'Saved Passwords' },
  settings: { w: 540, h: 520, title: 'Options' }
};

function createWindowManager(deps) {
  // deps: { settings, createTabs, onWindowReady }
  var mainStates = [];
  var dialogs = new Map(); // page -> BrowserWindow
  var dialogWcs = new Set();

  function createMainWindow(sessionData, show) {
    var win = new BrowserWindow({
      width: 1280,
      height: 860,
      minWidth: 640,
      minHeight: 420,
      show: false,
      frame: false,
      backgroundColor: '#c0c0c0',
      webPreferences: {
        preload: PRELOAD,
        contextIsolation: true,
        sandbox: true,
        webviewTag: false,
        spellcheck: false
      }
    });

    var state = {
      win: win,
      tabs: null,
      chromeWC: win.webContents
    };
    state.emitRenderer = function (channel, payload) {
      if (!win.isDestroyed()) win.webContents.send(channel, payload);
    };
    mainStates.push(state);

    state.tabs = deps.createTabs(win, state);

    win.loadFile(path.join(RENDERER, 'index.html'));

    win.webContents.on('did-finish-load', function () {
      var urls = sanitizeSessionList(sessionData && sessionData.urls, 40);
      if (urls.length) {
        var activeIdx = Number(sessionData && sessionData.active) || 0;
        if (activeIdx < 0 || activeIdx >= urls.length) activeIdx = 0;
        urls.forEach(function (u, i) {
          // Active tab loads now; background tabs queue for staggered loads.
          if (i === activeIdx) state.tabs.create(u, true);
          else state.tabs.createBackground(u);
        });
        if (state.tabs.count() > 0) {
          var list = state.tabs.list();
          if (list[activeIdx]) state.tabs.setActive(list[activeIdx].id);
        }
      } else {
        state.tabs.create(deps.settings.get('homepage'), true);
      }
      if (deps.onWindowReady) deps.onWindowReady(state);
      if (show !== false) win.show();
      state.emitRenderer('win:state', { maximized: win.isMaximized() });
      state.emitRenderer('win:active', { active: win.isFocused() });
    });

    win.on('maximize', function () {
      state.emitRenderer('win:state', { maximized: true });
    });
    win.on('unmaximize', function () {
      state.emitRenderer('win:state', { maximized: false });
    });
    win.on('focus', function () {
      state.emitRenderer('win:active', { active: true });
    });
    win.on('blur', function () {
      state.emitRenderer('win:active', { active: false });
    });

    win.webContents.on('will-navigate', function (e) {
      e.preventDefault(); // the chrome UI is local-only
    });
    win.webContents.setWindowOpenHandler(function () {
      return { action: 'deny' };
    });
    win.webContents.on('before-input-event', function (e, input) {
      if (deps.onKey && deps.onKey(input, state)) e.preventDefault();
    });

    win.on('close', function () {
      // Save while the state is still registered: quitting via the window
      // X runs before-quit only after all windows are gone. The closing
      // state is passed so multi-window sessions can exclude it.
      if (deps.onWindowClosing) deps.onWindowClosing(state);
    });
    win.on('closed', function () {
      var idx = mainStates.indexOf(state);
      if (idx !== -1) mainStates.splice(idx, 1);
    });

    return state;
  }

  function openDialog(page, payload, parent) {
    var spec = DIALOG_SPEC[page];
    if (!spec) return null;
    var existing = dialogs.get(page);
    if (existing && !existing.isDestroyed()) {
      if (payload != null) existing.webContents.send('payload', payload);
      existing.focus();
      return existing;
    }
    var win = new BrowserWindow({
      width: spec.w,
      height: spec.h,
      show: false,
      frame: false,
      resizable: page === 'source',
      backgroundColor: '#c0c0c0',
      parent: undefined,
      webPreferences: {
        preload: PRELOAD,
        contextIsolation: true,
        sandbox: true,
        webviewTag: false,
        spellcheck: false
      }
    });
    dialogs.set(page, win);
    var dialogWc = win.webContents;
    dialogWcs.add(dialogWc);
    win.loadFile(path.join(RENDERER, 'dialogs', page + '.html'));
    win.on('focus', function () {
      if (!win.isDestroyed()) win.webContents.send('win:active', { active: true });
    });
    win.on('blur', function () {
      if (!win.isDestroyed()) win.webContents.send('win:active', { active: false });
    });
    win.webContents.setWindowOpenHandler(function () {
      return { action: 'deny' };
    });
    win.webContents.on('will-navigate', function (e) {
      e.preventDefault(); // dialogs are local-only
    });
    win.webContents.on('did-finish-load', function () {
      if (payload != null) win.webContents.send('payload', payload);
      if (parent && !parent.isDestroyed()) {
        var b = parent.getBounds();
        var wb = win.getBounds();
        win.setPosition(
          Math.max(0, b.x + Math.round((b.width - wb.width) / 2)),
          Math.max(0, b.y + Math.round((b.height - wb.height) / 2))
        );
      }
      win.show();
    });
    win.on('closed', function () {
      dialogs.delete(page);
      dialogWcs.delete(dialogWc);
    });
    return win;
  }

  function byChrome(wc) {
    return (
      mainStates.find(function (s) { return s.chromeWC === wc; }) || null
    );
  }

  function isDialogWc(wc) {
    return dialogWcs.has(wc);
  }

  function findTabByWc(wc) {
    for (var i = 0; i < mainStates.length; i++) {
      var found = null;
      mainStates[i].tabs.each(function (t) {
        if (t.wc === wc) found = t;
      });
      if (found) return { state: mainStates[i], tab: found };
    }
    return null;
  }

  function eachMain(fn) {
    mainStates.slice().forEach(fn);
  }

  function primary() {
    return mainStates[0] || null;
  }

  function quit() {
    require('electron').app.quit();
  }

  return {
    createMainWindow: createMainWindow,
    openDialog: openDialog,
    byChrome: byChrome,
    isDialogWc: isDialogWc,
    findTabByWc: findTabByWc,
    eachMain: eachMain,
    primary: primary,
    quit: quit,
    dialogPages: function () { return Array.from(dialogs.keys()); }
  };
}

module.exports = { createWindowManager: createWindowManager };
