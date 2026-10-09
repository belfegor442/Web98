'use strict';

/**
 * Context menus (built per right-click, rendered by the retro chrome UI)
 * and the command executor shared by menus and keyboard shortcuts.
 */

var { dialog, shell, BrowserWindow } = require('electron');

function contextTemplate(params, tab) {
  var items = [];
  var hasSel = !!(params.selectionText && params.selectionText.length);
  var link = params.linkURL || '';

  items.push({ id: 'back', label: 'Back', enabled: tab.canBack });
  items.push({ id: 'forward', label: 'Forward', enabled: tab.canForward });
  items.push({ id: 'reload', label: 'Reload' });
  items.push({ id: 'stop', label: 'Stop', enabled: tab.loading });
  items.push({ sep: true });

  if (link) {
    items.push({ id: 'open-link-tab', label: 'Open Link in New Tab' });
    items.push({ id: 'copy-link', label: 'Copy Link Address' });
    items.push({ id: 'save-link', label: 'Save Link As...' });
    items.push({ sep: true });
  }
  if (params.isEditable) {
    items.push({ id: 'cut', label: 'Cut', enabled: hasSel || true });
    items.push({ id: 'copy', label: 'Copy', enabled: hasSel || true });
    items.push({ id: 'paste', label: 'Paste' });
    items.push({ id: 'delete', label: 'Delete' });
    items.push({ sep: true });
  } else if (hasSel) {
    items.push({ id: 'copy', label: 'Copy' });
    items.push({ sep: true });
  }
  if (params.mediaType === 'image' || params.srcURL) {
    items.push({ id: 'save-image', label: 'Save Image As...' });
    items.push({ id: 'copy-image', label: 'Copy Image' });
    items.push({ sep: true });
  }
  items.push({ id: 'select-all', label: 'Select All' });
  items.push({ sep: true });
  items.push({ id: 'view-source', label: 'View Source' });
  items.push({ id: 'save-page', label: 'Save Page As...' });
  items.push({ id: 'print', label: 'Print...' });
  items.push({ sep: true });
  items.push({ id: 'inspect', label: 'Inspect Element' });
  return items;
}

function runCommand(name, arg, ctx) {
  // ctx: { state, tabs, settings, windows, store, downloads }
  var tab = ctx.tabs.active();
  var win = ctx.state.win;

  switch (name) {
    case 'file:new-tab':
      ctx.tabs.create(arg || null, true);
      return true;
    case 'file:new-window':
      ctx.windows.createMainWindow(null, true);
      return true;
    case 'file:close-tab':
      if (tab) ctx.tabs.close(tab.id);
      return true;
    case 'file:close-window':
      win.close();
      return true;
    case 'file:open-location':
      ctx.state.emitRenderer('chrome:focus-address');
      return true;
    case 'file:save-page':
      savePage(tab);
      return true;
    case 'file:print':
      if (tab) tab.wc.print({ silent: false, printBackground: true });
      return true;
    case 'file:exit':
      ctx.windows.quit();
      return true;

    case 'edit:page': {
      if (!tab) return true;
      var ops = {
        undo: function () { tab.wc.undo(); },
        redo: function () { tab.wc.redo(); },
        cut: function () { tab.wc.cut(); },
        copy: function () { tab.wc.copy(); },
        paste: function () { tab.wc.paste(); },
        delete: function () { tab.wc.delete(); },
        'select-all': function () { tab.wc.selectAll(); }
      };
      if (ops[arg]) ops[arg]();
      return true;
    }

    case 'view:refresh':
      if (tab) tab.wc.reload();
      return true;
    case 'view:stop':
      if (tab && tab.loading) tab.wc.stop();
      return true;
    case 'view:home':
      ctx.tabs.load(tab, ctx.settings.get('homepage'));
      return true;
    case 'view:fullscreen':
      win.setFullScreen(!win.isFullScreen());
      return true;
    case 'view:zoom':
      ctx.tabs.setZoom(arg);
      ctx.settings.set('zoom', arg);
      return true;
    case 'view:toolbar':
      ctx.settings.set('toolbarVisible', !ctx.settings.get('toolbarVisible'));
      ctx.state.emitRenderer('settings:changed', ctx.settings.get());
      return true;
    case 'view:statusbar':
      ctx.settings.set('statusBar', !ctx.settings.get('statusBar'));
      ctx.state.emitRenderer('settings:changed', ctx.settings.get());
      return true;
    case 'view:devtools':
      if (tab) tab.wc.toggleDevTools();
      return true;
    case 'view:source':
      if (tab) viewSource(tab, ctx);
      return true;

    case 'tools:downloads':
      ctx.windows.openDialog('downloads', null, win);
      return true;
    case 'tools:history':
      ctx.windows.openDialog('history', null, win);
      return true;
    case 'tools:settings':
      ctx.windows.openDialog('settings', null, win);
      return true;
    case 'help:about':
      ctx.windows.openDialog('about', null, win);
      return true;
    case 'favorites:organize':
      ctx.windows.openDialog('favorites', null, win);
      return true;

    case 'nav:search':
      searchInNewTab(ctx);
      return true;
  }
  return false;
}

function savePage(tab) {
  if (!tab) return;
  var suggested = (tab.title || 'page').replace(/[\\/:*?"<>|]/g, '_').slice(0, 80) + '.html';
  dialog.showSaveDialog({
    title: 'Save Web Page',
    defaultPath: suggested,
    filters: [{ name: 'HTML', extensions: ['htm', 'html'] }]
  }).then(function (res) {
    if (res.canceled || !res.filePath) return;
    tab.wc.savePage(res.filePath, 'HTMLComplete').catch(function () {});
  });
}

function viewSource(tab, ctx) {
  tab.wc
    .executeJavaScript('document.documentElement.outerHTML', true)
    .then(function (html) {
      ctx.windows.openDialog('source', { html: html, url: tab.url }, ctx.state.win);
    })
    .catch(function () {
      ctx.windows.openDialog('source', { html: '<!-- unable to read document -->', url: tab.url }, ctx.state.win);
    });
}

function searchInNewTab(ctx) {
  var template = ctx.settings.searchTemplate();
  var base = template
    .split('%s')[0]
    .replace(/[?&=]+$/, '');
  ctx.tabs.create(base, true);
}

/**
 * Global browser accelerators, intercepting key input before page scripts.
 * Returns true when the key was consumed.
 */
function handleAccelerator(input, ctx) {
  if (input.type !== 'keyDown') return false;
  var key = input.key;
  var ctrl = input.control;
  var alt = input.alt;
  var shift = input.shift;
  var tabs = ctx.tabs;
  var tab = tabs.active();

  if (ctrl && !alt && !shift) {
    switch (key.toLowerCase()) {
      case 't':
        tabs.create(null, true);
        return true;
      case 'w':
        if (tab) tabs.close(tab.id);
        return true;
      case 'n':
        ctx.wm.createMainWindow(null, true);
        return true;
      case 'l':
        ctx.state.emitRenderer('chrome:focus-address');
        return true;
      case 'r':
        if (tab) tab.wc.reload();
        return true;
      case 'd':
        ctx.state.emitRenderer('chrome:add-favorite');
        return true;
      case 'f':
        ctx.state.emitRenderer('chrome:find');
        return true;
      case 'h':
        ctx.wm.openDialog('history', null, ctx.state.win);
        return true;
      case 'j':
        ctx.wm.openDialog('downloads', null, ctx.state.win);
        return true;
      case 'p':
        if (tab) tab.wc.print({ silent: false, printBackground: true });
        return true;
      case 's':
        if (tab) savePage(tab);
        return true;
      case '=':
      case '+':
        ctx.zoomBy(0.1);
        return true;
      case '-':
        ctx.zoomBy(-0.1);
        return true;
      case '0':
        ctx.setZoom(1);
        return true;
    }
    return false;
  }

  if (ctrl && shift && !alt && key.toLowerCase() === 't') {
    ctx.wm.createMainWindow(null, true);
    return true;
  }
  if (ctrl && shift && !alt && key.toLowerCase() === 'w') {
    ctx.state.win.close();
    return true;
  }

  if (alt && !ctrl && key === 'ArrowLeft') {
    if (tab && tab.wc.navigationHistory.canGoBack()) tab.wc.navigationHistory.goBack();
    return true;
  }
  if (alt && !ctrl && key === 'ArrowRight') {
    if (tab && tab.wc.navigationHistory.canGoForward()) tab.wc.navigationHistory.goForward();
    return true;
  }
  if (alt && !ctrl && key.toLowerCase() === 'd') {
    ctx.state.emitRenderer('chrome:focus-address');
    return true;
  }
  if (key === 'F5') {
    if (tab) tab.wc.reload();
    return true;
  }
  if (key === 'F11') {
    ctx.state.win.setFullScreen(!ctx.state.win.isFullScreen());
    return true;
  }
  if (key === 'F6') {
    ctx.state.emitRenderer('chrome:focus-address');
    return true;
  }
  if (key === 'Escape' && tab && tab.loading) {
    tab.wc.stop();
    return true;
  }
  return false;
}

module.exports = {
  contextTemplate: contextTemplate,
  runCommand: runCommand,
  savePage: savePage,
  viewSource: viewSource,
  handleAccelerator: handleAccelerator
};
