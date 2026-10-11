/* global window, document */
'use strict';

/**
 * Web98 chrome renderer: titlebar, menus, toolbar, address bar, tab strip,
 * side panels, find bar, status bar, context menus, dialogs.
 * Talks to the main process through the web98 preload bridge.
 */

(function () {
  var api = window.web98;
  var $ = function (id) { return document.getElementById(id); };

  /* ------------------------------ state ---------------------------------- */
  var nav = {
    url: '', title: '', loading: false, progress: 0,
    canBack: false, canForward: false, zoom: 1,
    zone: 'Internet zone', error: null
  };
  var tabs = [];
  var settings = null;
  var activeFloat = null; // single open floating element (menus, lists)
  var lastBounds = null;
  var sidePanelMode = null; // 'favorites' | 'history' | null
  var suggest = { items: [], idx: -1 };

  /* --------------------------- small helpers ------------------------------ */
  function el(tag, cls, parent) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (parent) parent.appendChild(e);
    return e;
  }
  function invoke(ch, data) { return api.invoke(ch, data); }
  function send(ch, data) { api.send(ch, data); }

  /* ------------------------- floating layer mgmt -------------------------- */
  function openFloat(elem) {
    if (activeFloat && activeFloat !== elem) closeFloat();
    activeFloat = elem;
    pushBounds();
  }
  function closeFloat() {
    if (!activeFloat) return;
    activeFloat = null;
    $('addr-list').hidden = true;
    $('addr-list').classList.remove('is-open');
    document.querySelectorAll('.ow-menu.is-open').forEach(function (m) {
      m.classList.remove('is-open');
    });
    pushBounds();
  }
  function floatOpen() { return !!activeFloat; }

  var lastPushedBoundsKey = '';
  var lastPushedPopup = null;
  var pushRaf = 0;
  var pushTrailing = null;

  function pushBoundsNow() {
    var ghost = $('content-ghost');
    if (!ghost) return;
    var r = ghost.getBoundingClientRect();
    var x = Math.round(r.left);
    var y = Math.round(r.top);
    var w = Math.round(r.width);
    var h = Math.round(r.height);
    var hide = false;
    if (activeFloat) {
      var f = activeFloat.getBoundingClientRect();
      var overlap = Math.ceil(f.bottom) - y;
      if (overlap > 0) {
        if (h - overlap >= 60) {
          y += overlap;
          h -= overlap;
        } else {
          hide = true;
        }
      }
    }
    lastBounds = { x: x, y: y, width: w, height: h };
    var key = x + ',' + y + ',' + w + ',' + h;
    if (key !== lastPushedBoundsKey) {
      lastPushedBoundsKey = key;
      invoke('chrome:bounds', lastBounds);
    }
    if (hide !== lastPushedPopup) {
      lastPushedPopup = hide;
      send('chrome:popup', { open: hide });
    }
  }

  function pushBounds() {
    // Coalesce bursts (resize streams, float open/close) to at most one
    // computation per frame plus a trailing flush after the stream settles.
    if (!pushRaf && typeof requestAnimationFrame === 'function') {
      pushRaf = requestAnimationFrame(function () {
        pushRaf = 0;
        pushBoundsNow();
      });
    }
    clearTimeout(pushTrailing);
    pushTrailing = setTimeout(function () {
      pushTrailing = null;
      pushBoundsNow();
    }, 120);
  }

  /* --------------------------- window controls ---------------------------- */
  $('titlebar').addEventListener('click', function (e) {
    var ctrl = e.target.closest('[data-control]');
    if (!ctrl) return;
    var c = ctrl.getAttribute('data-control');
    if (c === 'minimize') invoke('win:min');
    else if (c === 'maximize' || c === 'restore') invoke('win:max');
    else if (c === 'close') invoke('win:close');
  });
  // Native builds use frameless windows: the engine must start the move loop.
  // Electron ignores the unknown channel (allowlist) and uses -webkit-app-region;
  // double-click-to-maximize is detected natively on the second win:drag.
  $('titlebar').addEventListener('mousedown', function (e) {
    if (e.button !== 0 || e.target.closest('[data-control]')) return;
    send('win:drag');
  });
  api.on('win:state', function (s) {
    $('btn-max').setAttribute('data-control', s.maximized ? 'restore' : 'maximize');
    setTimeout(pushBounds, 60);
    setTimeout(pushBounds, 350);
  });
  api.on('win:active', function (s) {
    $('app').classList.toggle('is-active', !!s.active);
  });

  /* ------------------------------- menus ---------------------------------- */
  function editAction(arg) {
    var ae = document.activeElement;
    var editable = ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA');
    if (editable) {
      var map = {
        undo: 'undo', redo: 'redo', cut: 'cut', copy: 'copy',
        paste: 'paste', delete: 'delete', 'select-all': 'selectAll'
      };
      document.execCommand(map[arg]);
    } else {
      invoke('app:command', { name: 'edit:page', arg: arg });
    }
  }

  var MENUS = [
    {
      id: 'file', label: 'File', items: [
        { cmd: 'file:new-tab', label: 'New Tab', hint: 'Ctrl+T' },
        { cmd: 'file:new-window', label: 'New Window', hint: 'Ctrl+N' },
        { sep: true },
        { cmd: 'file:open-location', label: 'Open Location...', hint: 'Ctrl+L' },
        { cmd: 'file:save-page', label: 'Save Page As...', hint: 'Ctrl+S' },
        { cmd: 'file:print', label: 'Print...', hint: 'Ctrl+P' },
        { sep: true },
        { cmd: 'file:close-tab', label: 'Close Tab', hint: 'Ctrl+W' },
        { cmd: 'file:close-window', label: 'Close Window', hint: 'Ctrl+Shift+W' },
        { sep: true },
        { cmd: 'file:exit', label: 'Exit' }
      ]
    },
    {
      id: 'edit', label: 'Edit', items: [
        { act: function () { editAction('undo'); }, label: 'Undo', hint: 'Ctrl+Z' },
        { act: function () { editAction('redo'); }, label: 'Redo', hint: 'Ctrl+Y' },
        { sep: true },
        { act: function () { editAction('cut'); }, label: 'Cut', hint: 'Ctrl+X' },
        { act: function () { editAction('copy'); }, label: 'Copy', hint: 'Ctrl+C' },
        { act: function () { editAction('paste'); }, label: 'Paste', hint: 'Ctrl+V' },
        { act: function () { editAction('delete'); }, label: 'Delete', hint: 'Del' },
        { sep: true },
        { act: function () { editAction('select-all'); }, label: 'Select All', hint: 'Ctrl+A' },
        { sep: true },
        { act: function () { openFind(); }, label: 'Find on this Page...', hint: 'Ctrl+F' }
      ]
    },
    {
      id: 'view', label: 'View', items: [
        { cmd: 'view:stop', label: 'Stop', hint: 'Esc' },
        { cmd: 'view:refresh', label: 'Refresh', hint: 'F5' },
        { sep: true },
        { check: 'toolbarVisible', label: 'Toolbar' },
        { check: 'statusBar', label: 'Status Bar' },
        { sep: true },
        {
          label: 'Text Size', sub: [
            { cmd: 'view:zoom', arg: 0.75, label: '75%', radio: 'zoom' },
            { cmd: 'view:zoom', arg: 0.9, label: '90%', radio: 'zoom' },
            { cmd: 'view:zoom', arg: 1, label: '100%', radio: 'zoom' },
            { cmd: 'view:zoom', arg: 1.25, label: '125%', radio: 'zoom' },
            { cmd: 'view:zoom', arg: 1.5, label: '150%', radio: 'zoom' },
            { cmd: 'view:zoom', arg: 2, label: '200%', radio: 'zoom' }
          ]
        },
        { cmd: 'view:source', label: 'Source' },
        { sep: true },
        { cmd: 'view:fullscreen', label: 'Full Screen', hint: 'F11' }
      ]
    },
    {
      id: 'favorites', label: 'Favorites', items: [
        { emitDialog: 'add-favorite', label: 'Add to Favorites...', hint: 'Ctrl+D' },
        { cmd: 'favorites:organize', label: 'Organize Favorites...' },
        { sep: true },
        { dyn: 'bookmarks' }
      ]
    },
    {
      id: 'tools', label: 'Tools', items: [
        { cmd: 'tools:history', label: 'History...', hint: 'Ctrl+H' },
        { cmd: 'tools:downloads', label: 'Downloads...', hint: 'Ctrl+J' },
        { sep: true },
        { cmd: 'tools:settings', label: 'Internet Options...' }
      ]
    },
    {
      id: 'help', label: 'Help', items: [
        { cmd: 'help:about', label: 'About Web98' }
      ]
    }
  ];

  var bookmarkItems = [];

  function buildMenus() {
    var bar = $('menubar');
    bar.textContent = '';
    MENUS.forEach(function (menu) {
      var wrap = el('div', 'ow-menu', bar);
      wrap.setAttribute('data-menu', menu.id);
      var label = el('button', 'ow-menu__label', wrap);
      label.textContent = menu.label;
      var drop = el('div', 'ow-menu__dropdown', wrap);
      label.addEventListener('mousedown', function (e) {
        e.preventDefault();
        e.stopPropagation();
        if (wrap.classList.contains('is-open')) closeFloat();
        else openMenu(wrap);
      });
      wrap.addEventListener('mouseenter', function () {
        if (activeFloat && activeFloat.classList.contains('ow-menu__dropdown') && !wrap.classList.contains('is-open')) {
          openMenu(wrap);
        }
      });
      drop._menu = menu;
    });
    var spacer = el('span', 'ow-menubar__spacer', bar);
    void spacer;
    var throbber = el('span', 'ow-throbber', bar);
    throbber.id = 'throbber';
    throbber.title = 'Working';
  }

  function openMenu(wrap) {
    closeFloat();
    var drop = wrap.querySelector('.ow-menu__dropdown');
    renderMenuItems(drop, drop._menu);
    wrap.classList.add('is-open');
    activeFloat = drop;
    pushBounds();
  }

  function renderMenuItems(drop, menu) {
    drop.textContent = '';
    menu.items.forEach(function (item) {
      if (item.sep) { el('div', 'ow-menu__sep', drop); return; }
      if (item.dyn === 'bookmarks') {
        if (!bookmarkItems.length) {
          var none = el('div', 'ow-menu__item is-disabled', drop);
          el('span', 'ow-menu__check', none);
          el('span', null, none).textContent = '(empty)';
        } else {
          bookmarkItems.slice(0, 12).forEach(function (b) {
            var it = el('div', 'ow-menu__item', drop);
            el('span', 'ow-menu__check', it);
            var t = el('span', null, it);
            t.textContent = b.name;
            it.addEventListener('click', function () {
              closeFloat();
              navigate(b.url);
            });
          });
        }
        return;
      }
      if (item.sub) {
        var head = el('div', 'ow-menu__item', drop);
        el('span', 'ow-menu__check', head);
        el('span', null, head).textContent = item.label;
        el('i', 'ow-menu__hint', head).textContent = '\u25b6';
        el('div', 'ow-menu__sep', drop);
        item.sub.forEach(function (sub) { renderMenuItem(drop, sub); });
        return;
      }
      renderMenuItem(drop, item);
    });
  }

  function renderMenuItem(drop, item) {
    var it = el('div', 'ow-menu__item', drop);
    var check = el('span', 'ow-menu__check', it);
    if (item.check != null && settings) {
      check.textContent = settings[item.check] ? '\u2713' : '';
    } else if (item.radio != null && settings) {
      var cur = settings.zoom != null ? settings.zoom : nav.zoom;
      check.textContent = Math.abs(cur - item.arg) < 0.001 ? '\u2022' : '';
    }
    el('span', null, it).textContent = item.label;
    if (item.hint) el('i', 'ow-menu__hint', it).textContent = item.hint;
    it.addEventListener('click', function () {
      closeFloat();
      executeItem(item);
    });
  }

  function executeItem(item) {
    if (item.act) { item.act(); return; }
    if (item.emit) { send(item.emit, null); return; }
    if (item.emitDialog === 'add-favorite') { openAddFavorite(); return; }
    if (item.cmd) invoke('app:command', { name: item.cmd, arg: item.arg });
  }

  /* ------------------------------ toolbar --------------------------------- */
  var TOOLBAR = [
    { action: 'back', label: 'Back', run: function () { invoke('nav:back'); } },
    { action: 'forward', label: 'Forward', run: function () { invoke('nav:forward'); } },
    { sep: true },
    { action: 'stop', label: 'Stop', run: function () { invoke('nav:stop'); } },
    { action: 'refresh', label: 'Refresh', run: function () { invoke('nav:reload'); } },
    { action: 'home', label: 'Home', run: function () { invoke('nav:home'); } },
    { sep: true },
    { action: 'search', label: 'Search', run: function () { invoke('app:command', { name: 'nav:search' }); } },
    { action: 'favorites', label: 'Favorites', run: function () { togglePanel('favorites'); } },
    { action: 'history', label: 'History', run: function () { togglePanel('history'); } },
    { sep: true },
    { action: 'print', label: 'Print', run: function () { invoke('app:command', { name: 'file:print' }); } }
  ];
  var toolbarBtns = {};

  function buildToolbar() {
    var bar = $('toolbar');
    bar.textContent = '';
    TOOLBAR.forEach(function (b) {
      if (b.sep) { el('span', 'ow-toolbar-sep', bar); return; }
      var btn = el('button', 'ow-btn', bar);
      btn.setAttribute('data-action', b.action);
      btn.title = b.label;
      el('span', 'ow-btn__icon', btn);
      el('span', 'ow-btn__label', btn).textContent = b.label;
      btn.addEventListener('click', b.run);
      toolbarBtns[b.action] = btn;
    });
  }

  function updateToolbar() {
    if (toolbarBtns.back) toolbarBtns.back.disabled = !nav.canBack;
    if (toolbarBtns.forward) toolbarBtns.forward.disabled = !nav.canForward;
    if (toolbarBtns.stop) toolbarBtns.stop.disabled = !nav.loading;
    ['back', 'forward', 'stop', 'search', 'favorites', 'history'].forEach(function (a) {
      if (toolbarBtns[a]) toolbarBtns[a].classList.toggle('is-active-panel',
        (a === 'favorites' && sidePanelMode === 'favorites') ||
        (a === 'history' && sidePanelMode === 'history'));
    });
  }

  /* ---------------------------- address bar ------------------------------- */
  var addressInput = $('address');

  function navigate(address) {
    return invoke('nav:go', { address: address }).then(function (r) {
      if (r && r.ok && r.url) addressInput.value = r.url;
      closeSuggest();
    });
  }

  function go() { navigate(addressInput.value); }

  addressInput.addEventListener('focus', function () {
    addressInput.select();
  });
  addressInput.addEventListener('keydown', function (e) {
    if (e.key === 'Enter') {
      if (suggest.idx >= 0 && suggest.items[suggest.idx]) {
        addressInput.value = suggest.items[suggest.idx].url;
      }
      go();
      e.preventDefault();
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      if (!suggest.items.length) return;
      e.preventDefault();
      suggest.idx += e.key === 'ArrowDown' ? 1 : -1;
      if (suggest.idx < 0) suggest.idx = suggest.items.length - 1;
      if (suggest.idx >= suggest.items.length) suggest.idx = 0;
      highlightSuggest();
    } else if (e.key === 'Escape') {
      closeSuggest();
      addressInput.value = nav.url;
      addressInput.blur();
    }
  });
  var suggestTimer = null;
  addressInput.addEventListener('input', function () {
    clearTimeout(suggestTimer);
    var q = addressInput.value.trim();
    if (!q) { closeSuggest(); return; }
    suggestTimer = setTimeout(function () {
      invoke('omni:suggest', { q: q }).then(renderSuggest);
    }, 120);
  });

  function renderSuggest(items) {
    suggest.items = items || [];
    suggest.idx = -1;
    var list = $('addr-list');
    list.textContent = '';
    if (!suggest.items.length) { closeSuggest(); return; }
    suggest.items.forEach(function (s, i) {
      var row = el('div', 'ow-address-item', list);
      el('span', null, row).textContent = s.title;
      var small = el('small', null, row);
      small.textContent = s.type === 'bookmark' ? 'Favorite' : 'History';
      row.addEventListener('mousedown', function (e) {
        e.preventDefault();
        addressInput.value = s.url;
        go();
      });
      row._i = i;
    });
    list.hidden = false;
    list.classList.add('is-open');
    openFloat(list);
  }
  function highlightSuggest() {
    var rows = $('addr-list').children;
    for (var i = 0; i < rows.length; i++) {
      rows[i].classList.toggle('is-hot', i === suggest.idx);
    }
  }
  function closeSuggest() {
    suggest.items = [];
    suggest.idx = -1;
    if (activeFloat && activeFloat.id === 'addr-list') activeFloat = null;
    $('addr-list').hidden = true;
    $('addr-list').classList.remove('is-open');
    pushBounds();
  }

  $('addr-drop').addEventListener('mousedown', function (e) {
    e.preventDefault();
    if (activeFloat && activeFloat.id === 'addr-list') { closeSuggest(); return; }
    invoke('history:list', { limit: 12 }).then(function (rows) {
      renderSuggest(rows.map(function (h) {
        return { title: h.title || h.url, url: h.url, type: 'history' };
      }));
    });
  });
  $('btn-go').addEventListener('click', go);
  $('btn-links').addEventListener('click', function () { togglePanel('favorites'); });

  /* ------------------------------ tabs ------------------------------------ */
  function byTabId(id) {
    for (var i = 0; i < tabs.length; i++) {
      if (tabs[i].id === id) return tabs[i];
    }
    return null;
  }

  function renderTabs() {
    var strip = $('tabstrip');
    var rows = strip._rows;
    var reusable = !!rows && rows.length === tabs.length;
    if (reusable) {
      for (var i = 0; i < tabs.length; i++) {
        if (rows[i].id !== tabs[i].id) { reusable = false; break; }
      }
    }
    if (!reusable) {
      strip.textContent = '';
      rows = [];
      tabs.forEach(function (t) {
        var tab = el('div', 'ow-tab', strip);
        var spinner = el('span', 'ow-tab__spinner', tab);
        var icon = el('span', 'ow-tab__icon', tab);
        var titleEl = el('span', 'ow-tab__title', tab);
        var close = el('button', 'ow-tab__close', tab);
        close.textContent = '\u00d7';
        close.title = 'Close';
        var row = { id: t.id, el: tab, spinner: spinner, icon: icon, titleEl: titleEl };
        close.addEventListener('click', function (e) {
          e.stopPropagation();
          invoke('tabs:close', { id: row.id });
        });
        tab.addEventListener('mousedown', function (e) {
          if (e.button === 1) {
            e.preventDefault();
            invoke('tabs:close', { id: row.id });
            return;
          }
          if (e.button === 0) {
            var cur = byTabId(row.id);
            if (cur && !cur.active) invoke('tabs:activate', { id: row.id });
          }
        });
        tab.addEventListener('contextmenu', function (e) {
          e.preventDefault();
          var cur = byTabId(row.id);
          if (cur) showTabMenu(e, cur);
        });
        rows.push(row);
      });
      strip._rows = rows;
      var plus = el('button', 'ow-tab-new', strip);
      plus.textContent = '+';
      plus.title = 'New Tab (Ctrl+T)';
      plus.addEventListener('click', function () {
        invoke('tabs:create', {});
      });
    }
    tabs.forEach(function (t, i) {
      var row = rows[i];
      var cls = 'ow-tab' + (t.active ? ' is-active' : '') + (t.loading ? ' is-loading' : '');
      if (row.el.className !== cls) row.el.className = cls;
      row.el.title = t.title || t.url;
      var spinCls = 'ow-tab__spinner' + (t.loading ? ' is-active' : '');
      if (row.spinner.className !== spinCls) row.spinner.className = spinCls;
      var bg = (t.favicon && /^data:|^https?:/i.test(t.favicon)) ? 'url("' + t.favicon + '")' : '';
      if (bg) {
        if (row.icon.style.backgroundImage !== bg) row.icon.style.backgroundImage = bg;
      } else if (row.icon.style.backgroundImage) {
        row.icon.style.backgroundImage = '';
      }
      var txt = t.title || t.url || 'New Tab';
      if (row.titleEl.textContent !== txt) row.titleEl.textContent = txt;
    });
  }

  function showTabMenu(e, t) {
    showCtxMenu(e.clientX, e.clientY, [
      { label: 'New Tab', act: function () { invoke('tabs:create', {}); } },
      { sep: true },
      {
        label: 'Close Tab', act: function () {
          invoke('tabs:close', { id: t.id });
        }
      },
      {
        label: 'Close Other Tabs', act: function () {
          tabs.forEach(function (o) {
            if (o.id !== t.id) invoke('tabs:close', { id: o.id });
          });
        }
      },
      {
        label: 'Reload Tab', act: function () {
          invoke('tabs:activate', { id: t.id });
          invoke('nav:reload');
        }
      }
    ]);
  }

  /* --------------------------- side panel --------------------------------- */
  function togglePanel(mode) {
    sidePanelMode = sidePanelMode === mode ? null : mode;
    var panel = $('sidepanel');
    if (!sidePanelMode) {
      panel.classList.remove('is-open');
      updateToolbar();
      pushBounds();
      return;
    }
    panel.classList.add('is-open');
    $('sidepanel-title').textContent = sidePanelMode === 'favorites' ? 'Favorites' : 'History';
    renderPanel();
    updateToolbar();
    pushBounds();
  }

  function renderPanel() {
    var body = $('sidepanel-body');
    var foot = $('sidepanel-foot');
    body.textContent = '';
    foot.textContent = '';
    if (sidePanelMode === 'favorites') {
      invoke('bookmarks:list').then(function (all) {
        if (sidePanelMode !== 'favorites') return;
        var items = all.filter(function (b) { return b.url; });
        if (!items.length) {
          el('div', 'panel-empty', body).textContent = 'No favorites yet.';
        }
        items.forEach(function (b) {
          var row = el('div', 'panel-item', body);
          var img = el('img', null, row);
          img.src = '../../assets/ie5/assets/favorites.png';
          el('span', null, row).textContent = b.name;
          row.title = b.url;
          row.addEventListener('click', function () { navigate(b.url); });
        });
        var add = el('a', null, foot);
        add.textContent = 'Add...';
        add.addEventListener('click', openAddFavorite);
        var org = el('a', null, foot);
        org.textContent = 'Organize...';
        org.addEventListener('click', function () {
          invoke('app:command', { name: 'favorites:organize' });
        });
      });
    } else {
      invoke('history:list', { limit: 60 }).then(function (rows) {
        if (sidePanelMode !== 'history') return;
        if (!rows.length) {
          el('div', 'panel-empty', body).textContent = 'No history yet.';
        }
        rows.forEach(function (h) {
          var row = el('div', 'panel-item', body);
          var img = el('img', null, row);
          img.src = '../../assets/ie5/assets/ie-logo.png';
          el('span', null, row).textContent = h.title || h.url;
          row.title = h.url;
          row.addEventListener('click', function () { navigate(h.url); });
        });
        var all = el('a', null, foot);
        all.textContent = 'View All History';
        all.addEventListener('click', function () {
          invoke('app:command', { name: 'tools:history' });
        });
      });
    }
  }

  $('sidepanel-close').addEventListener('click', function () { togglePanel(sidePanelMode); });

  /* ----------------------------- find bar --------------------------------- */
  function openFind() {
    $('findbar').classList.add('is-open');
    $('find-input').focus();
    $('find-input').select();
    pushBounds();
  }
  api.on('chrome:find', openFind);
  function closeFind() {
    $('findbar').classList.remove('is-open');
    invoke('app:find', { action: 'stop' });
    pushBounds();
  }
  $('find-close').addEventListener('click', closeFind);
  $('find-next').addEventListener('click', function () {
    invoke('app:find', { action: 'next', term: $('find-input').value });
  });
  $('find-prev').addEventListener('click', function () {
    invoke('app:find', { action: 'prev', term: $('find-input').value });
  });
  $('find-input').addEventListener('keydown', function (e) {
    if (e.key === 'Enter') invoke('app:find', { action: 'next', term: $('find-input').value });
    if (e.key === 'Escape') closeFind();
  });

  /* --------------------------- context menu -------------------------------- */
  var ctxActive = false;

  api.on('ctx', function (data) {
    var x = data.x;
    var y = data.y;
    if (lastBounds) {
      x = lastBounds.x + data.x;
      y = lastBounds.y + data.y;
    }
    var items = (data.items || []).map(function (i) {
      if (i.sep) return { sep: true };
      return {
        id: i.id,
        label: i.label,
        enabled: i.enabled !== false,
        act: i.enabled !== false ? function () {
          send('ctx:command', { id: i.id });
        } : null
      };
    });
    showCtxMenu(x, y, items);
  });

  function showCtxMenu(x, y, items) {
    closeFloat();
    var layer = $('ctxlayer');
    layer.textContent = '';
    layer.style.pointerEvents = 'auto';
    var menu = el('div', 'ctx-menu', layer);
    items.forEach(function (i) {
      if (i.sep) { el('div', 'ctx-sep', menu); return; }
      var row = el('div', 'ctx-item' + (i.enabled === false ? ' is-disabled' : ''), menu);
      el('span', 'ctx-check', row);
      el('span', null, row).textContent = i.label;
      if (i.enabled !== false && i.act) {
        row.addEventListener('click', function () {
          disposeCtx();
          i.act();
        });
      }
    });
    var r = menu.getBoundingClientRect();
    menu.style.left = Math.max(2, Math.min(x, window.innerWidth - r.width - 4)) + 'px';
    menu.style.top = Math.max(2, Math.min(y, window.innerHeight - r.height - 4)) + 'px';
    activeFloat = menu;
    ctxActive = true;
    pushBounds();
  }
  function disposeCtx() {
    if (!ctxActive) return;
    ctxActive = false;
    $('ctxlayer').textContent = '';
    $('ctxlayer').style.pointerEvents = 'none';
    activeFloat = null;
    pushBounds();
  }
  $('ctxlayer').addEventListener('mousedown', function (e) {
    if (e.target === $('ctxlayer')) disposeCtx();
  });

  /* -------------------------- in-window dialog ---------------------------- */
  var currentMini = null;
  $('dialogs').addEventListener('mousedown', function (e) {
    if (e.target === $('dialogs') && currentMini) currentMini.cancel();
  });

  function openMini(opts) {
    var host = $('dialogs');
    host.textContent = '';
    host.classList.add('is-open');
    var dlg = el('div', 'minidlg', host);
    var title = el('div', 'minidlg__title', dlg);
    el('span', null, title).textContent = opts.title;
    var close = el('button', 'minidlg__close', title);
    close.textContent = '\u00d7';
    var body = el('div', 'minidlg__body', dlg);
    if (opts.icon) {
      var img = el('img', 'minidlg__icon', body);
      img.src = opts.icon;
    }
    var text = el('div', 'minidlg__text', body);
    text.innerHTML = opts.bodyHtml || '';
    var buttons = el('div', 'minidlg__buttons', dlg);
    function done(v) {
      currentMini = null;
      host.classList.remove('is-open');
      host.textContent = '';
      if (opts.onResult) opts.onResult(v);
    }
    currentMini = {
      cancel: function () { done(null); }
    };
    close.addEventListener('click', function () { done(null); });
    (opts.buttons || [{ label: 'OK', value: true, default: true }]).forEach(function (b) {
      var btn = el('button', 'w98-btn' + (b.default ? ' is-default' : ''), buttons);
      btn.textContent = b.label;
      btn.addEventListener('click', function () {
        done(b.value !== undefined ? b.value : b.label);
      });
    });
    var firstInput = text.querySelector('input, select');
    if (firstInput) firstInput.focus();
    else {
      var defBtn = buttons.querySelector('.is-default') || buttons.firstChild;
      if (defBtn) defBtn.focus();
    }
    return { root: dlg, text: text, done: done };
  }

  /* -------------------------- add favorite --------------------------------- */
  function openAddFavorite() {
    if (!nav.url || !/^https?:|^file:/i.test(nav.url)) {
      openMini({
        title: 'Add Favorite',
        icon: '../../assets/ie5/assets/favorites.png',
        bodyHtml: '<p>There is no page to add right now.</p>',
        buttons: [{ label: 'OK', value: true, default: true }]
      });
      return;
    }
    invoke('bookmarks:list').then(function (all) {
      var folders = all.filter(function (b) { return !b.url; });
      var opts = folders.map(function (f) {
        return '<option value="' + f.id + '">' + escapeHtml(f.name) + '</option>';
      }).join('');
      var dlg = openMini({
        title: 'Add Favorite',
        icon: '../../assets/ie5/assets/favorites.png',
        bodyHtml:
          '<label>Name:</label><input type="text" id="fav-name" value="' + escapeHtml(nav.title || nav.url) + '" />' +
          '<label>Folder:</label><select id="fav-folder"><option value="0">Favorites</option>' + opts + '</select>' +
          '<p style="color:#404040">' + escapeHtml(nav.url) + '</p>',
        buttons: [
          { label: 'OK', value: 'ok', default: true },
          { label: 'Cancel', value: null }
        ],
        onResult: function (v) {
          if (v !== 'ok') return;
          var name = (document.getElementById('fav-name') || {}).value || nav.url;
          var folder = parseInt((document.getElementById('fav-folder') || {}).value || '0', 10) || 0;
          invoke('bookmarks:add', { name: name, url: nav.url, parent: folder });
        }
      });
      void dlg;
    });
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  /* --------------------------- global events ------------------------------ */
  api.on('tabs', function (list) {
    tabsPrimed = true;
    tabs = list || [];
    renderTabs();
  });

  var lastWinTitle = '';
  var tabsPrimed = false;
  api.on('nav', function (n) {
    nav = n;
    if (document.activeElement !== addressInput) addressInput.value = n.url || '';
    var title = (n.title || 'New Tab') + ' - Web98';
    if (title !== lastWinTitle) {
      lastWinTitle = title;
      $('win-title').textContent = title;
      invoke('win:title', title);
    }
    $('status').textContent = n.loading
      ? 'Opening page ' + (n.url || '') + '...'
      : n.error ? (n.error.desc || n.error.url || 'Error') : 'Done';
    $('zone').textContent = n.zone || '';
    $('progress').classList.toggle('is-active', n.loading);
    $('progress-bar').style.width = Math.round((n.progress || 0) * 100) + '%';
    $('throbber').classList.toggle('is-active', n.loading);
    updateToolbar();
  });

  api.on('chrome:focus-address', function () {
    addressInput.focus();
    addressInput.select();
  });
  api.on('chrome:add-favorite', openAddFavorite);
  api.on('chrome:tab-focused', function () {
    if (ctxActive) disposeCtx();
    else if (floatOpen()) closeFloat();
  });
  api.on('settings:changed', function (s) {
    settings = s;
    applySettings();
  });
  api.on('bookmarks:changed', refreshBookmarks);
  api.on('history:changed', function () {
    if (sidePanelMode === 'history') renderPanel();
  });

  function refreshBookmarks() {
    invoke('bookmarks:list').then(function (all) {
      bookmarkItems = all.filter(function (b) { return b.url; });
      if (sidePanelMode === 'favorites') renderPanel();
    });
  }

  function applySettings() {
    if (!settings) return;
    document.querySelector('.ow-toolbar').style.display = settings.toolbarVisible ? '' : 'none';
    document.querySelector('.ow-statusbar').hidden = settings.statusBar === false;
    pushBounds();
  }

  /* ------------------------------ boot ------------------------------------ */
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') {
      if (ctxActive) { disposeCtx(); return; }
      if (floatOpen()) { closeFloat(); return; }
      if ($('findbar').classList.contains('is-open')) closeFind();
    }
  });
  document.addEventListener('mousedown', function (e) {
    if (!floatOpen()) return;
    if (ctxActive) return;
    if (e.target.closest('.ow-menu__dropdown')) return;
    if (e.target.closest('.ow-menu__label')) return;
    if (e.target.closest('#addr-list')) return;
    if (e.target.closest('#addr-drop')) return;
    closeFloat();
  });

  window.addEventListener('resize', function () {
    setTimeout(pushBounds, 30);
    setTimeout(pushBounds, 250);
  });

  if (window.ResizeObserver) {
    new ResizeObserver(function () { pushBounds(); }).observe(document.body);
  }

  function boot() {
    buildMenus();
    buildToolbar();
    return Promise.all([
      invoke('settings:get'),
      invoke('tabs:list')
    ]).then(function (r) {
      settings = r[0];
      // A live 'tabs' push may be newer than this reply; don't clobber it.
      if (!tabsPrimed) {
        tabs = r[1] || [];
        renderTabs();
      }
      applySettings();
      refreshBookmarks();
      pushBounds();
    });
  }
  boot();
})();
