'use strict';

/**
 * BrowserCore tab management: one WebContentsView per tab, stacked inside
 * the main window. Handles navigation lifecycle, events and error pages.
 */

var path = require('path');
var { WebContentsView, shell } = require('electron');
var { contextTemplate } = require('./menus');
var {
  isLoadableUrl,
  isAllowedNavigation,
  isWebUrl,
  isSafeExternalUrl
} = require('../shared/safeurl');

var nextTabId = 1;
var PRELOAD = path.join(__dirname, 'preload-tab.js');
var ERROR_PAGE = path.join(__dirname, '..', 'renderer', 'error.html');

function createTabManager(opts) {
  var win = opts.win;
  var emit = opts.emit; // function(channel, payload)
  var hooks = opts.hooks || {};
  // hooks: { onCommitted(tab), onTitle(tab), onFavicon(tab), onPopup(url, tab) }

  var tabs = [];
  var activeId = null;
  var bounds = { x: 0, y: 0, width: 0, height: 0 };
  var hiddenForPopup = false;

  function snapshot() {
    return tabs.map(function (t) {
      return {
        id: t.id,
        url: t.url,
        title: t.title,
        favicon: t.favicon,
        loading: t.loading,
        progress: t.progress,
        active: t.id === activeId
      };
    });
  }

  function emitTabs() {
    emit('tabs', snapshot());
  }

  function navState(tab) {
    if (!tab || tab.id !== activeId) return;
    var url = tab.url || '';
    emit('nav', {
      tabId: tab.id,
      url: url,
      title: tab.title,
      loading: tab.loading,
      progress: tab.progress,
      canBack: canGoBack(tab),
      canForward: canGoForward(tab),
      zoom: tab.zoom,
      zone: zoneFor(url),
      error: tab.error || null
    });
  }

  function canGoBack(tab) {
    try {
      return tab.wc.navigationHistory.canGoBack();
    } catch (e) {
      return false;
    }
  }
  function canGoForward(tab) {
    try {
      return tab.wc.navigationHistory.canGoForward();
    } catch (e) {
      return false;
    }
  }

  function zoneFor(url) {
    if (/^file:|^http:\/\/(localhost|127\.)/i.test(url)) return 'Local intranet';
    if (/^https?:/i.test(url)) return 'Internet zone';
    return '';
  }

  function applyBounds() {
    tabs.forEach(function (t) {
      t.view.setBounds(bounds);
    });
  }

  function wire(tab) {
    var wc = tab.wc;

    wc.setWindowOpenHandler(function (details) {
      var u = details.url || '';
      if (isSafeExternalUrl(u)) {
        shell.openExternal(u);
      } else if (isLoadableUrl(u)) {
        var opener = '';
        try { opener = wc.getURL(); } catch (e) { /* noop */ }
        if (isAllowedNavigation(u, { fromWeb: isWebUrl(opener) })) create(u, true);
      }
      return { action: 'deny' };
    });

    wc.on('will-navigate', function (e, url) {
      if (isSafeExternalUrl(url)) {
        e.preventDefault();
        shell.openExternal(url);
        return;
      }
      var current = '';
      try { current = wc.getURL(); } catch (err) { /* noop */ }
      if (!isAllowedNavigation(url, { fromWeb: isWebUrl(current) })) e.preventDefault();
    });

    wc.on('did-start-loading', function () {
      tab.loading = true;
      tab.progress = 0.05;
      tab.error = null;
      emitTabs();
      navState(tab);
    });

    wc.on('loading-progress', function (e, p) {
      tab.progress = p;
      navState(tab);
    });

    wc.on('did-stop-loading', function () {
      tab.loading = false;
      tab.progress = 1;
      emitTabs();
      navState(tab);
      if (!tab.suppressCommit && /^https?:/i.test(tab.url) && hooks.onCommitted) {
        hooks.onCommitted(tab);
      }
      tab.suppressCommit = false;
    });

    wc.on('did-navigate', function (e, url) {
      if (tab.errorPageShown) {
        // Internal error page load — keep the failed URL visible in the UI.
        tab.errorPageShown = false;
        return;
      }
      tab.url = url;
      tab.error = null;
      tab.crashedOnce = false;
      emitTabs();
      navState(tab);
    });

    wc.on('did-navigate-in-page', function (e, url, isMainFrame) {
      if (isMainFrame) {
        tab.url = url;
        emitTabs();
        navState(tab);
      }
    });

    wc.on('page-title-updated', function (e, title) {
      tab.title = title;
      emitTabs();
      navState(tab);
      if (hooks.onTitle) hooks.onTitle(tab);
    });

    wc.on('page-favicon-updated', function (e, icons) {
      tab.favicon = (icons && icons[0]) || '';
      emitTabs();
      if (hooks.onFavicon) hooks.onFavicon(tab);
    });

    wc.on('did-fail-load', function (e, code, desc, url, isMainFrame) {
      if (!isMainFrame || code === -3) return; // -3 = aborted
      if (tab.errorPageShown) return;
      showError(tab, code, desc, url);
    });

    wc.on('render-process-gone', function (e, details) {
      if (details.reason === 'clean-exit') return;
      if (!tab.crashedOnce) {
        tab.crashedOnce = true;
        wc.reload();
      } else {
        showError(tab, -1, 'The tab process crashed', tab.url);
      }
    });

    wc.on('context-menu', function (e, params) {
      tab.lastCtx = { params: params, time: Date.now() };
      emit('ctx', {
        x: params.x,
        y: params.y,
        items: contextTemplate(params, {
          canBack: canGoBack(tab),
          canForward: canGoForward(tab),
          loading: tab.loading
        })
      });
    });

    wc.on('before-input-event', function (e, input) {
      if (opts.onKey && opts.onKey(input)) e.preventDefault();
    });

    wc.on('focus', function () {
      emit('chrome:tab-focused');
    });

    wc.on('destroyed', function () {
      close(tab.id, true);
    });
  }

  function showError(tab, code, desc, url) {
    if (tab.errorPageShown) return;
    var kind = /ERR_CERT|ERR_SSL|ERR_BAD_SSL/i.test(String(desc || '')) || isCertCode(code)
      ? 'cert'
      : 'net';
    tab.error = { code: code, desc: desc, url: url, kind: kind };
    tab.errorPageShown = true;
    tab.suppressCommit = true;
    tab.url = url || tab.url;
    try {
      tab.wc.loadFile(ERROR_PAGE, {
        query: {
          kind: kind,
          code: String(code),
          desc: String(desc || ''),
          url: String(url || '')
        }
      });
    } catch (err) {
      /* ignore */
    }
    emitTabs();
    navState(tab);
  }

  function isCertCode(code) {
    // Chromium certificate errors occupy -200..-299.
    return code <= -200 && code >= -299;
  }

  function create(url, activate) {
    if (activate == null) activate = true;
    var tab = {
      id: nextTabId++,
      view: null,
      wc: null,
      url: '',
      title: 'New Tab',
      favicon: '',
      loading: false,
      progress: 0,
      zoom: opts.getZoom ? opts.getZoom() : 1,
      error: null
    };
    tab.view = new WebContentsView({
      webPreferences: {
        preload: PRELOAD,
        sandbox: true,
        contextIsolation: true,
        nodeIntegration: false,
        webviewTag: false,
        spellcheck: false,
        partition: 'persist:web98'
      }
    });
    tab.wc = tab.view.webContents;
    tab.view.setBounds(bounds);
    tab.view.setVisible(false);
    win.contentView.addChildView(tab.view);
    wire(tab);
    tabs.push(tab);
    try {
      tab.wc.setZoomFactor(tab.zoom);
    } catch (e) { /* noop */ }
    if (activate) setActive(tab.id);
    emitTabs();
    if (url) load(tab, url);
    return tab;
  }

  function load(tab, url) {
    if (!tab || !tab.wc) return false;
    if (isSafeExternalUrl(url)) {
      shell.openExternal(url);
      return true;
    }
    if (!isLoadableUrl(url)) return false;
    var p = tab.wc.loadURL(url);
    if (p && typeof p.catch === 'function') {
      p.catch(function (err) {
        if (tab.errorPageShown || !tab.wc) return;
        showError(tab, -1, String((err && err.message) || err || 'Navigation failed'), url);
      });
    }
    return true;
  }

  function get(id) {
    return tabs.find(function (t) { return t.id === id; }) || null;
  }

  function active() {
    return get(activeId);
  }

  function setActive(id) {
    var tab = get(id);
    if (!tab) return;
    if (activeId === id) {
      tab.view.setVisible(!hiddenForPopup);
      tab.wc.focus();
      navState(tab);
      return;
    }
    activeId = id;
    // Raise above sibling views: last child draws on top.
    win.contentView.removeChildView(tab.view);
    win.contentView.addChildView(tab.view);
    tabs.forEach(function (t) {
      t.view.setVisible(t.id === activeId && !hiddenForPopup);
    });
    tab.wc.focus();
    emitTabs();
    navState(tab);
  }

  function close(id, destroyed) {
    var idx = tabs.findIndex(function (t) { return t.id === id; });
    if (idx === -1) return;
    var tab = tabs[idx];
    tabs.splice(idx, 1);
    if (!destroyed) {
      try {
        tab.view.removeFromParentView();
      } catch (e) { /* noop */ }
      try {
        tab.wc.destroy();
      } catch (e) { /* noop */ }
    }
    if (activeId === id) {
      activeId = null;
      var next = tabs[Math.min(idx, tabs.length - 1)];
      if (next) setActive(next.id);
    }
    emitTabs();
    if (tabs.length === 0 && hooks.onLastClosed) hooks.onLastClosed();
  }

  function setPopupHidden(hidden) {
    hiddenForPopup = hidden;
    tabs.forEach(function (t) {
      t.view.setVisible(t.id === activeId && !hidden);
    });
    if (!hidden) {
      var t = active();
      if (t) t.wc.focus();
    }
  }

  function setBounds(b) {
    if (!b) return;
    var x = Number(b.x);
    var y = Number(b.y);
    var width = Number(b.width);
    var height = Number(b.height);
    if (![x, y, width, height].every(Number.isFinite)) return;
    bounds = { x: x, y: y, width: width, height: height };
    applyBounds();
  }

  function setZoom(factor) {
    var t = active();
    if (!t) return;
    t.zoom = factor;
    try {
      t.wc.setZoomFactor(factor);
    } catch (e) { /* noop */ }
    navState(t);
  }

  function each(fn) {
    tabs.forEach(fn);
  }

  return {
    create: create,
    close: close,
    setActive: setActive,
    get: get,
    active: active,
    list: snapshot,
    load: load,
    setBounds: setBounds,
    setPopupHidden: setPopupHidden,
    setZoom: setZoom,
    navState: function () { navState(active()); },
    emitTabs: emitTabs,
    each: each,
    showError: showError,
    count: function () { return tabs.length; }
  };
}

module.exports = { createTabManager: createTabManager };
