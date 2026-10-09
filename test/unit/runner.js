'use strict';

/**
 * Web98 unit tests — runs INSIDE Electron because better-sqlite3 is built
 * against the Electron ABI:  npm test  ->  electron test/unit/runner.js
 *
 * Covers: address-bar parsing, SQLite store, settings merge/persistence,
 * context menu template, keyboard accelerator table.
 */

var os = require('os');
var path = require('path');
var fs = require('fs');

var electronApp = null;
try {
  var electron = require('electron');
  if (electron && electron.app) electronApp = electron.app;
} catch (e) {
  /* plain node fallback (will fail only on better-sqlite3 ABI) */
}

/* ------------------------------ harness ---------------------------------- */
var passed = 0;
var failed = 0;
var failures = [];

function test(name, fn) {
  try {
    fn();
    passed++;
    console.log('  PASS  ' + name);
  } catch (err) {
    failed++;
    failures.push(name + ' — ' + err.message);
    console.log('  FAIL  ' + name + ' — ' + err.message);
  }
}
function eq(actual, expected, msg) {
  var a = JSON.stringify(actual);
  var e = JSON.stringify(expected);
  if (a !== e) throw new Error((msg ? msg + ': ' : '') + 'expected ' + e + ', got ' + a);
}
function ok(value, msg) {
  if (!value) throw new Error(msg || 'expected truthy value');
}

/* ------------------------------- urlbar ----------------------------------- */
var { parseAddress, isKnownScheme } = require('../../src/shared/urlbar');

function runUrlbar() {
  var DDG = 'https://duckduckgo.com/?q=%s';

  test('urlbar: empty input', function () {
    eq(parseAddress('').type, 'empty');
    eq(parseAddress('   ').type, 'empty');
    eq(parseAddress(null).type, 'empty');
  });

  test('urlbar: full URL passthrough', function () {
    var r = parseAddress('https://example.com/a?b=c');
    eq(r.type, 'url');
    eq(r.url, 'https://example.com/a?b=c');
  });

  test('urlbar: scheme is case-insensitive', function () {
    var r = parseAddress('HTTPS://Example.COM');
    eq(r.type, 'url');
    eq(r.url, 'HTTPS://Example.COM');
  });

  test('urlbar: bare domain gets http://', function () {
    var r = parseAddress('example.com');
    eq(r.type, 'url');
    eq(r.url, 'http://example.com');
  });

  test('urlbar: bare domain with port and path', function () {
    var r = parseAddress('example.com:8080/path?q=1');
    eq(r.type, 'url');
    eq(r.url, 'http://example.com:8080/path?q=1');
  });

  test('urlbar: localhost', function () {
    eq(parseAddress('localhost:3000').url, 'http://localhost:3000');
    eq(parseAddress('localhost').url, 'http://localhost');
  });

  test('urlbar: IPv4 address', function () {
    eq(parseAddress('192.168.1.10').url, 'http://192.168.1.10');
    eq(parseAddress('127.0.0.1/admin').url, 'http://127.0.0.1/admin');
  });

  test('urlbar: phrase is a search', function () {
    var r = parseAddress('how to make a bomb', { searchTemplate: DDG });
    eq(r.type, 'search');
    eq(r.url, 'https://duckduckgo.com/?q=how%20to%20make%20a%20bomb');
  });

  test('urlbar: single word is a search', function () {
    var r = parseAddress('weather', { searchTemplate: DDG });
    eq(r.type, 'search');
    eq(r.query, 'weather');
  });

  test('urlbar: custom search template', function () {
    var r = parseAddress('electron ipc', {
      searchTemplate: 'https://www.google.com/search?q=%s'
    });
    eq(r.url, 'https://www.google.com/search?q=electron%20ipc');
  });

  test('urlbar: template without %s appends', function () {
    var r = parseAddress('x', { searchTemplate: 'https://s.example/?q=' });
    eq(r.url, 'https://s.example/?q=x');
  });

  test('urlbar: search terms are encoded', function () {
    var r = parseAddress('a b&c=d', { searchTemplate: DDG });
    eq(r.url, 'https://duckduckgo.com/?q=a%20b%26c%3Dd');
  });

  test('urlbar: javascript: commands', function () {
    var r = parseAddress('javascript:document.title=1');
    eq(r.type, 'javascript');
    eq(r.url, 'document.title=1');
  });

  test('urlbar: view-source passes through', function () {
    var r = parseAddress('view-source:https://example.com');
    eq(r.type, 'url');
    eq(r.url, 'view-source:https://example.com');
  });

  test('urlbar: mailto and data pass through', function () {
    eq(parseAddress('mailto:a@b.c').url, 'mailto:a@b.c');
    eq(parseAddress('data:text/html,<b>x</b>').url, 'data:text/html,<b>x</b>');
  });

  test('urlbar: about:blank is a url, other about: is search', function () {
    eq(parseAddress('about:blank').type, 'url');
    eq(parseAddress('about:config').type, 'search');
  });

  test('urlbar: isKnownScheme', function () {
    ok(isKnownScheme('http'), 'http known');
    ok(isKnownScheme('HTTPS'), 'https known');
    ok(!isKnownScheme('gopher2'), 'gopher2 unknown');
  });
}

/* -------------------------------- store ----------------------------------- */
var { openStore } = require('../../src/main/store');

function runStore() {
  var dir = fs.mkdtempSync(path.join(os.tmpdir(), 'web98-test-'));
  var dbPath = path.join(dir, 'test.db');
  var store = openStore(dbPath);

  test('store: settings roundtrip (JSON)', function () {
    store.setSetting('homepage', 'https://example.org/');
    store.setSetting('zoom', 1.5);
    store.setSetting('flag', true);
    eq(store.getSetting('homepage'), 'https://example.org/');
    eq(store.getSetting('zoom'), 1.5);
    eq(store.getSetting('flag'), true);
    eq(store.getSetting('missing', 'fb'), 'fb');
    eq(store.getSetting('missing'), undefined);
  });

  test('store: allSettings returns all keys', function () {
    var all = store.allSettings();
    eq(all.homepage, 'https://example.org/');
    eq(all.zoom, 1.5);
  });

  test('store: history insert and recent order', function () {
    store.addHistory('https://a.example/', 'A', 1000);
    store.addHistory('https://b.example/', 'B', 2000);
    store.addHistory('https://c.example/', 'C', 3000);
    var rows = store.history({ limit: 10 });
    eq(rows.length, 3);
    eq(rows[0].title, 'C', 'newest first');
    eq(rows[2].title, 'A');
  });

  test('store: history search matches url and title', function () {
    var rows = store.history({ q: 'b.example' });
    eq(rows.length, 1);
    eq(rows[0].title, 'B');
    var byTitle = store.history({ q: 'C' });
    eq(byTitle.length, 1);
    eq(byTitle[0].url, 'https://c.example/');
  });

  test('store: history delete one', function () {
    var rows = store.history({ q: 'a.example' });
    store.deleteHistory(rows[0].id);
    eq(store.history({ limit: 10 }).length, 2);
  });

  test('store: history prune keeps newest N', function () {
    for (var i = 0; i < 30; i++) store.addHistory('https://p' + i + '.example/', 'P' + i, 4000 + i);
    store.pruneHistory(5);
    eq(store.countHistory(), 5);
    eq(store.history({ limit: 5 })[0].title, 'P29');
  });

  test('store: history clear', function () {
    store.clearHistory();
    eq(store.countHistory(), 0);
  });

  test('store: bookmarks add/list hierarchy order', function () {
    var folder = store.addBookmark({ name: 'Work', url: '', parent: 0 });
    ok(folder && folder.id, 'folder created');
    eq(folder.url, '', 'folder has empty url');
    store.addBookmark({ name: 'First', url: 'https://1.example/', parent: folder.id });
    store.addBookmark({ name: 'Second', url: 'https://2.example/', parent: folder.id });
    store.addBookmark({ name: 'Root item', url: 'https://root.example/', parent: 0 });
    var all = store.listBookmarks();
    eq(all.length, 4);
    eq(all[0].name, 'Work', 'root folder first (parent 0)');
    eq(all[1].name, 'Root item', 'then root-level item');
    eq(all[2].name, 'First', 'folder children follow');
    eq(all[3].name, 'Second');
  });

  test('store: bookmarks update', function () {
    var b = store.listBookmarks().find(function (x) { return x.name === 'First'; });
    store.updateBookmark(b.id, { name: 'First (renamed)', url: b.url });
    var again = store.getBookmark(b.id);
    eq(again.name, 'First (renamed)');
    eq(again.url, 'https://1.example/');
  });

  test('store: bookmarks delete', function () {
    var b = store.listBookmarks().find(function (x) { return x.url === 'https://2.example/'; });
    store.deleteBookmark(b.id);
    eq(store.listBookmarks().filter(function (x) { return x.url === 'https://2.example/'; }).length, 0);
  });

  test('store: persistence across reopen', function () {
    store.close();
    var store2 = openStore(dbPath);
    eq(store2.countHistory(), 0, 'history still cleared');
    ok(store2.listBookmarks().length >= 3, 'bookmarks persisted');
    store2.setSetting('zoom', 2);
    store2.close();
    var store3 = openStore(dbPath);
    eq(store3.getSetting('zoom'), 2, 'setting persisted');
    store3.close();
  });

  try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* noop */ }
}

/* ------------------------------- settings --------------------------------- */
var { createSettings, defaults } = require('../../src/main/settings');

function runSettings() {
  var dir = fs.mkdtempSync(path.join(os.tmpdir(), 'web98-set-'));
  var dbPath = path.join(dir, 'set.db');
  var store = openStore(dbPath);
  var paths = { renderer: 'C:\\app\\renderer' };

  test('settings: defaults', function () {
    var d = defaults(paths);
    eq(d.homepage, 'file:///C:/app/renderer/startpage.html');
    eq(d.searchEngine, 'DuckDuckGo');
    eq(d.zoom, 1);
    eq(d.sessionRestore, true);
    ok(d.engines.length >= 4, 'built-in engines');
  });

  test('settings: get/set and change notification', function () {
    var s = createSettings(store, paths);
    var events = [];
    s.onChange(function (k, v) { events.push([k, v]); });
    s.set('zoom', 1.25);
    eq(s.get('zoom'), 1.25);
    eq(events.length, 1);
    eq(events[0][0], 'zoom');
    eq(events[0][1], 1.25);
  });

  test('settings: searchTemplate follows engine', function () {
    var s = createSettings(store, paths);
    eq(s.searchTemplate(), 'https://duckduckgo.com/?q=%s');
    s.set('searchEngine', 'Google');
    eq(s.searchTemplate(), 'https://www.google.com/search?q=%s');
    s.set('searchEngine', 'Nonexistent');
    eq(s.searchTemplate(), 'https://duckduckgo.com/?q=%s', 'falls back to first engine');
  });

  test('settings: persistence + engine merge without duplicates', function () {
    var s = createSettings(store, paths);
    s.set('searchEngine', 'Wikipedia');
    s.set('engines', [
      { name: 'Google', template: 'https://www.google.com/search?q=%s' }, // duplicate of built-in
      { name: 'MyEngine', template: 'https://me.example/?q=%s' }
    ]);
    s.set('permissions', { 'https://x.example': { geolocation: true } });

    var s2 = createSettings(store, paths);
    eq(s2.get('searchEngine'), 'Wikipedia');
    eq(s2.searchTemplate(), 'https://en.wikipedia.org/w/index.php?search=%s');
    var names = s2.get('engines').map(function (e) { return e.name; });
    eq(names.filter(function (n) { return n === 'Google'; }).length, 1, 'no Google dup');
    ok(names.indexOf('MyEngine') !== -1, 'custom engine kept');
    eq(names[0], 'DuckDuckGo', 'built-ins lead');
    eq(s2.get('permissions')['https://x.example'].geolocation, true, 'permissions persisted');
  });

  store.close();
  try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) { /* noop */ }
}

/* -------------------------- menus / accelerators -------------------------- */

function runMenus() {
  var menus = require('../../src/main/menus');

  test('menus: context template basics', function () {
    var items = menus.contextTemplate({ x: 0, y: 0 }, { canBack: true, canForward: false, loading: false });
    var ids = items.map(function (i) { return i.id || 'sep'; });
    ok(ids.indexOf('back') !== -1, 'has back');
    ok(ids.indexOf('reload') !== -1, 'has reload');
    var fwd = items.find(function (i) { return i.id === 'forward'; });
    eq(fwd.enabled, false, 'forward disabled without history');
    var back = items.find(function (i) { return i.id === 'back'; });
    eq(back.enabled, true, 'back enabled with history');
  });

  test('menus: link and image context items', function () {
    var items = menus.contextTemplate(
      { linkURL: 'https://x.example/', srcURL: 'https://x.example/i.png', mediaType: 'image' },
      { canBack: false, canForward: false, loading: false }
    );
    var ids = items.map(function (i) { return i.id; });
    ok(ids.indexOf('open-link-tab') !== -1, 'open link in new tab');
    ok(ids.indexOf('save-image') !== -1, 'save image');
    ok(ids.indexOf('copy-link') !== -1, 'copy link');
  });

  function fakeCtx() {
    var calls = [];
    var tab = {
      id: 1, zoom: 1, url: 'https://a.example/', title: 'A', loading: false,
      wc: {
        reload: function () { calls.push('reload'); },
        stop: function () { calls.push('stop'); },
        print: function () { calls.push('print'); },
        navigationHistory: {
          canGoBack: function () { return true; },
          canGoForward: function () { return false; },
          goBack: function () { calls.push('back'); },
          goForward: function () { calls.push('forward'); }
        }
      }
    };
    return {
      calls: calls,
      tab: tab,
      ctx: {
        tabs: {
          active: function () { return tab; },
          create: function (u) { calls.push('create:' + u); return tab; },
          close: function (id) { calls.push('close:' + id); },
          load: function (t, u) { calls.push('load:' + u); },
          setZoom: function (z) { calls.push('zoom:' + z); }
        },
        settings: {
          get: function (k) { return k === 'homepage' ? 'file:///home.html' : null; },
          set: function (k, v) { calls.push('set:' + k + '=' + v); }
        },
        state: {
          win: { close: function () { calls.push('win.close'); } },
          emitRenderer: function (ch) { calls.push('emit:' + ch); }
        },
        wm: {
          createMainWindow: function () { calls.push('new-window'); },
          openDialog: function (p) { calls.push('dialog:' + p); },
          quit: function () { calls.push('quit'); }
        },
        windows: {
          createMainWindow: function () { calls.push('new-window'); },
          openDialog: function (p) { calls.push('dialog:' + p); },
          quit: function () { calls.push('quit'); }
        },
        store: {},
        setZoom: function (z) { calls.push('setZoom:' + z); },
        zoomBy: function (d) { calls.push('zoomBy:' + d); }
      }
    };
  }

  test('menus: runCommand maps commands', function () {
    var f = fakeCtx();
    eq(menus.runCommand('file:new-tab', null, f.ctx), true);
    eq(menus.runCommand('view:refresh', null, f.ctx), true);
    eq(menus.runCommand('view:home', null, f.ctx), true);
    eq(menus.runCommand('tools:settings', null, f.ctx), true);
    eq(menus.runCommand('file:exit', null, f.ctx), true);
    ok(f.calls.indexOf('create:null') !== -1 || f.calls.indexOf('create:undefined') !== -1 ||
      f.calls.some(function (c) { return c.indexOf('create:') === 0; }), 'tab created');
    ok(f.calls.indexOf('reload') !== -1, 'reload called');
    ok(f.calls.indexOf('load:file:///home.html') !== -1, 'home loaded');
    ok(f.calls.indexOf('dialog:settings') !== -1, 'settings dialog');
    ok(f.calls.indexOf('quit') !== -1, 'quit called');
    eq(menus.runCommand('no:such', null, f.ctx), false, 'unknown command returns false');
  });

  test('menus: accelerators (Ctrl+T, F5, Alt+Left, Ctrl+Shift+T)', function () {
    var f = fakeCtx();
    var mk = function (key, mods) {
      return Object.assign({ type: 'keyDown', key: key, control: false, alt: false, shift: false }, mods || {});
    };
    eq(menus.handleAccelerator(mk('t', { control: true }), f.ctx), true, 'Ctrl+T handled');
    ok(f.calls.some(function (c) { return c.indexOf('create:') === 0; }), 'created tab');
    eq(menus.handleAccelerator(mk('F5'), f.ctx), true, 'F5 handled');
    ok(f.calls.indexOf('reload') !== -1, 'reload on F5');
    eq(menus.handleAccelerator(mk('ArrowLeft', { alt: true }), f.ctx), true, 'Alt+Left handled');
    ok(f.calls.indexOf('back') !== -1, 'goBack on Alt+Left');
    eq(menus.handleAccelerator(mk('t', { control: true, shift: true }), f.ctx), true, 'Ctrl+Shift+T');
    ok(f.calls.indexOf('new-window') !== -1, 'new window created');
    eq(menus.handleAccelerator(mk('z', { control: true }), f.ctx), false, 'unmapped combo not consumed');
    eq(menus.handleAccelerator({ type: 'keyUp', key: 't', control: true }, f.ctx), false, 'keyUp ignored');
  });
}

/* --------------------------------- main ----------------------------------- */
function runAll() {
  console.log('Web98 unit tests');
  runUrlbar();
  runStore();
  runSettings();
  runMenus();

  console.log('');
  console.log(passed + ' passed, ' + failed + ' failed');
  if (failures.length) {
    failures.forEach(function (f) { console.log('  ' + f); });
  }
  return failed ? 1 : 0;
}

if (electronApp) {
  var code = runAll();
  electronApp.exit(code);
} else {
  process.exit(runAll());
}
