/*
 * Internet Explorer 5 window for Web98.
 * Renders the .ow-* chrome from assets/ie5/theme.css and browses the bundled
 * Pages.* content. Each call to IE5.open() creates an independent instance.
 */
var IE5 = (function () {
  'use strict';

  var IE_ICON = 'assets/ie5/assets/ie-logo.png';

  var TOOLBAR = [
    'back', 'forward', '|',
    'stop', 'refresh', 'home', '|',
    'search', 'favorites', 'history', '|',
    'mail', 'print'
  ];

  var LABELS = {
    back: 'Back',
    forward: 'Forward',
    stop: 'Stop',
    refresh: 'Refresh',
    home: 'Home',
    search: 'Search',
    favorites: 'Favorites',
    history: 'History',
    mail: 'Mail',
    print: 'Print'
  };

  function div(parent, cls) {
    var n = document.createElement('div');
    if (cls) n.className = cls;
    if (parent) parent.appendChild(n);
    return n;
  }

  function esc(s) {
    return String(s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  function normalize(raw) {
    var s = String(raw == null ? '' : raw).trim();
    if (!s) return null;
    if (/^about:/i.test(s)) return s.toLowerCase();
    if (/^[a-z][a-z0-9+.-]*:\/\//i.test(s)) return s;
    if (/\s/.test(s)) s = s.replace(/\s+/g, '');
    if (!/^[a-z]+:/i.test(s)) s = 'http://' + s;
    return s;
  }

  var instances = {};

  function create(opts) {
    opts = opts || {};

    /* singleton window id: just focus the existing instance */
    if (opts.id && instances[opts.id] && WM.list().has(opts.id)) {
      WM.focus(opts.id);
      return instances[opts.id];
    }
    if (opts.id) delete instances[opts.id];

    var state = { history: [], index: -1, loading: false, timer: null };
    var el = {};
    var api = {};

    var win = WM.open({
      id: opts.id || null,
      title: 'Microsoft Internet Explorer',
      icon: IE_ICON,
      width: opts.width || 780,
      height: opts.height || 560,
      maximized: !!opts.maximized
    });

    buildChrome();

    var prevClose = win.onClose;
    win.onClose = function () {
      document.removeEventListener('mousedown', closePopups, true);
      document.removeEventListener('keydown', onDocKey);
      clearTimeout(state.timer);
      delete instances[win.id];
      if (prevClose) prevClose();
    };

    /* ----------------------------- chrome -------------------------------- */
    function buildChrome() {
      var body = win.body;

      /* menu bar */
      el.menubar = div(body, 'ow-menubar');
      buildMenus(el.menubar);
      div(el.menubar, 'ow-menubar__spacer');
      el.throbber = div(el.menubar, 'ow-throbber');
      el.throbber.title = 'Working';

      /* toolbar */
      el.toolbar = div(body, 'ow-toolbar');
      el.btns = {};
      TOOLBAR.forEach(function (t) {
        if (t === '|') {
          div(el.toolbar, 'ow-toolbar-sep');
          return;
        }
        var b = document.createElement('button');
        b.className = 'ow-btn';
        b.type = 'button';
        b.dataset.action = t;
        b.title = LABELS[t];
        b.innerHTML =
          '<span class="ow-btn__icon"></span>' +
          '<span class="ow-btn__label">' + LABELS[t] + '</span>';
        b.addEventListener('click', function () {
          toolbarAction(t);
        });
        el.toolbar.appendChild(b);
        el.btns[t] = b;
      });

      /* address bar */
      el.addressbar = div(body, 'ow-addressbar');
      var lab = div(el.addressbar, 'ow-address-label');
      lab.textContent = 'Address';

      var combo = div(el.addressbar, 'ow-address-combo');
      el.input = document.createElement('input');
      el.input.className = 'ow-address-input';
      el.input.type = 'text';
      el.input.spellcheck = false;
      el.input.setAttribute('aria-label', 'Address');
      el.input.addEventListener('keydown', function (e) {
        if (e.key === 'Enter') {
          e.preventDefault();
          navigate(el.input.value);
        }
      });
      combo.appendChild(el.input);

      el.dropBtn = document.createElement('button');
      el.dropBtn.className = 'ow-address-dropdown';
      el.dropBtn.type = 'button';
      el.dropBtn.title = 'Show visited pages';
      el.dropBtn.addEventListener('click', function () {
        toggleAddressList();
      });
      combo.appendChild(el.dropBtn);

      el.list = div(combo, 'ow-address-list');

      var go = document.createElement('button');
      go.className = 'ow-go';
      go.type = 'button';
      go.innerHTML = '<span class="ow-go__icon"></span>Go';
      go.addEventListener('click', function () {
        navigate(el.input.value);
      });
      el.addressbar.appendChild(go);

      el.links = div(el.addressbar, 'ow-loc-links');
      el.links.textContent = 'Links \u00bb';
      el.links.title = 'Show Favorites';
      el.links.addEventListener('click', function () {
        togglePanel('favorites');
      });

      el.panel = div(el.addressbar, 'ow-panel');

      /* page area */
      el.viewport = div(body, 'ow-viewport');
      el.viewport.addEventListener('click', onViewportClick);
      el.viewport.addEventListener('submit', onViewportSubmit);
      el.viewport.addEventListener('mouseover', function (e) {
        var a = e.target.closest ? e.target.closest('a[href]') : null;
        if (a && !state.loading) setStatus(a.getAttribute('href'));
      });
      el.viewport.addEventListener('mouseout', function (e) {
        if (e.target.closest && e.target.closest('a[href]') && !state.loading) setStatus('Done');
      });

      /* status bar */
      var sb = div(body, 'ow-statusbar');
      el.status = div(sb, 'ow-status-text');
      el.status.textContent = 'Done';
      el.progress = div(sb, 'ow-progress');
      el.progress.innerHTML = '<span class="ow-progress__bar"></span>';
      var z1 = div(sb, 'ow-zone');
      div(z1, 'ow-zone__lock');
      var z2 = div(sb, 'ow-zone');
      z2.textContent = 'Internet zone';

      document.addEventListener('mousedown', closePopups, true);
      document.addEventListener('keydown', onDocKey);

      updateChrome();
    }

    function buildMenus(menubar) {
      menuDefs().forEach(function (m) {
        var wrap = div(menubar, 'ow-menu');
        var lab = document.createElement('button');
        lab.className = 'ow-menu__label';
        lab.type = 'button';
        lab.textContent = m.label;
        lab.addEventListener('click', function () {
          var was = wrap.classList.contains('is-open');
          closeMenus();
          if (!was) wrap.classList.add('is-open');
        });
        lab.addEventListener('mouseenter', function () {
          if (anyMenuOpen() && !wrap.classList.contains('is-open')) {
            closeMenus();
            wrap.classList.add('is-open');
          }
        });
        wrap.appendChild(lab);

        var dd = div(wrap, 'ow-menu__dropdown');
        m.items.forEach(function (it) {
          if (it.sep) {
            div(dd, 'ow-menu__sep');
            return;
          }
          var item = div(dd, 'ow-menu__item' + (it.disabled ? ' is-disabled' : ''));
          var check = div(item, 'ow-menu__check');
          if (it.checked) check.textContent = '\u2713';
          var span = document.createElement('span');
          span.textContent = it.label;
          item.appendChild(span);
          if (it.hint) {
            var hint = div(item, 'ow-menu__hint');
            hint.textContent = it.hint;
          }
          if (!it.disabled) {
            item.addEventListener('click', function () {
              closeMenus();
              if (it.act) it.act();
            });
          }
        });
      });
    }

    function menuDefs() {
      var siteItems = Pages.SITES.map(function (s) {
        return {
          label: s.title.replace(/\s*\u2014.*$/, ''),
          act: function () { navigate(s.url); }
        };
      });

      return [
        {
          label: 'File',
          items: [
            { label: 'New Window', hint: 'Ctrl+N', act: function () { IE5.open({}); } },
            { label: 'Open...', hint: 'Ctrl+O', act: openLocation },
            { sep: true },
            { label: 'Save As...', disabled: true },
            { label: 'Print...', hint: 'Ctrl+P', act: doPrint },
            { label: 'Print Preview...', disabled: true },
            { sep: true },
            { label: 'Close', act: function () { WM.close(win.id); } }
          ]
        },
        {
          label: 'Edit',
          items: [
            { label: 'Cut', hint: 'Ctrl+X', disabled: true },
            { label: 'Copy', hint: 'Ctrl+C', disabled: true },
            { label: 'Paste', hint: 'Ctrl+V', disabled: true },
            { sep: true },
            { label: 'Select All', hint: 'Ctrl+A', act: selectAll },
            { label: 'Find (on This Page)...', hint: 'Ctrl+F', disabled: true }
          ]
        },
        {
          label: 'View',
          items: [
            { label: 'Refresh', hint: 'F5', act: reload },
            { label: 'Stop', hint: 'Esc', act: stopLoad },
            { sep: true },
            { label: 'Source', act: viewSource },
            { label: 'Full Screen', hint: 'F11', disabled: true },
            { sep: true },
            { label: 'Status Bar', checked: true, disabled: true },
            { label: 'Explorer Bar', disabled: true }
          ]
        },
        {
          label: 'Favorites',
          items: [
            { label: 'Add to Favorites...', disabled: true },
            { label: 'Organize Favorites...', disabled: true },
            { sep: true }
          ].concat(siteItems)
        },
        {
          label: 'Tools',
          items: [
            { label: 'Mail and News', disabled: true },
            { label: 'Synchronize...', disabled: true },
            { sep: true },
            { label: 'Internet Options...', act: internetOptions }
          ]
        },
        {
          label: 'Help',
          items: [
            { label: 'About Internet Explorer', act: aboutIE }
          ]
        }
      ];
    }

    /* ---------------------------- navigation ----------------------------- */
    function normalizeAndResolve(raw) {
      var url = normalize(raw);
      if (!url) return null;
      var r = Pages.resolve(url);
      if (!r) return { url: url, route: 'dns', params: { url: url } };
      var params = {};
      Object.keys(r.params || {}).forEach(function (k) { params[k] = r.params[k]; });
      params.url = r.url || url;
      return { url: r.url || url, route: r.route, params: params };
    }

    function navigate(raw) {
      var entry = normalizeAndResolve(raw);
      if (!entry) return;
      state.history = state.history.slice(0, state.index + 1);
      state.history.push(entry);
      state.index = state.history.length - 1;
      loadCurrent();
    }

    function goHistory(delta) {
      var next = state.index + delta;
      if (next < 0 || next >= state.history.length) return;
      state.index = next;
      loadCurrent();
    }

    function reload() {
      if (state.index < 0) return;
      loadCurrent();
    }

    function stopLoad() {
      if (!state.loading) return;
      clearTimeout(state.timer);
      state.timer = null;
      finishLoad();
    }

    function loadCurrent() {
      var entry = state.history[state.index];
      if (!entry) return;
      clearTimeout(state.timer);
      state.loading = true;
      updateChrome();
      el.input.value = entry.url;
      el.throbber.classList.add('is-active');
      el.progress.classList.add('is-active');
      setStatus('Opening page ' + entry.url + '...');
      state.timer = setTimeout(function () {
        state.timer = null;
        finishLoad();
      }, 420 + Math.random() * 460);
    }

    function finishLoad() {
      var entry = state.history[state.index];
      if (!entry) return;
      state.loading = false;

      el.viewport.innerHTML = Pages.render(entry.route, entry.params);
      el.viewport.scrollTop = 0;

      var meta = Pages.meta(entry.route, entry.params);
      WM.setTitle(win.id, meta.title + ' - Microsoft Internet Explorer');
      if (document.activeElement !== el.input) el.input.value = entry.url;

      el.throbber.classList.remove('is-active');
      el.progress.classList.remove('is-active');
      setStatus('Done');
      updateChrome();
    }

    function updateChrome() {
      if (!el.btns) return;
      el.btns.back.disabled = state.index <= 0;
      el.btns.forward.disabled = state.index >= state.history.length - 1;
      el.btns.stop.disabled = !state.loading;
      el.btns.home.disabled = false;
    }

    function setStatus(text) {
      el.status.textContent = text;
    }

    /* ---------------------------- popups/menus --------------------------- */
    function anyMenuOpen() {
      return !!el.menubar.querySelector('.ow-menu.is-open');
    }
    function closeMenus() {
      var open = el.menubar.querySelectorAll('.ow-menu.is-open');
      for (var i = 0; i < open.length; i++) open[i].classList.remove('is-open');
    }
    function closePopups(e) {
      if (e && e.target && e.target.closest) {
        if (
          e.target.closest(
            '.ow-menu, .ow-panel, .ow-address-list, .ow-address-dropdown, ' +
            '.ow-loc-links, [data-action="favorites"], [data-action="history"]'
          )
        ) {
          return;
        }
      }
      if (el.panel) el.panel.classList.remove('is-open');
      if (el.list) el.list.classList.remove('is-open');
      closeMenus();
    }
    function onDocKey(e) {
      if (e.key === 'Escape') {
        if (state.loading) stopLoad();
        closePopups();
      }
    }

    function togglePanel(kind) {
      if (el.panel.dataset.kind === kind && el.panel.classList.contains('is-open')) {
        el.panel.classList.remove('is-open');
        return;
      }
      el.panel.innerHTML = '';
      el.panel.dataset.kind = kind;
      var title = div(el.panel, 'ow-panel__title');
      title.textContent = kind === 'favorites' ? 'Favorites' : 'History';

      var items;
      if (kind === 'favorites') {
        items = Pages.SITES.map(function (s) {
          return { url: s.url, title: s.title };
        });
      } else {
        items = historyItems();
      }

      if (!items.length) {
        var empty = div(el.panel, 'ow-panel__item');
        empty.textContent = '(empty)';
        empty.style.color = '#808080';
      }
      items.forEach(function (it) {
        var d = div(el.panel, 'ow-panel__item');
        d.textContent = it.title;
        d.addEventListener('click', function () {
          el.panel.classList.remove('is-open');
          navigate(it.url);
        });
      });
      el.panel.classList.add('is-open');
    }

    function historyItems() {
      var seen = {};
      var out = [];
      for (var i = state.history.length - 1; i >= 0; i--) {
        var h = state.history[i];
        if (seen[h.url]) continue;
        seen[h.url] = 1;
        var meta = Pages.meta(h.route, h.params);
        out.push({ url: h.url, title: meta.title });
      }
      return out;
    }

    function toggleAddressList() {
      if (el.list.classList.contains('is-open')) {
        el.list.classList.remove('is-open');
        return;
      }
      el.list.innerHTML = '';
      var seen = {};
      var items = [];
      Pages.SITES.forEach(function (s) {
        if (seen[s.url]) return;
        seen[s.url] = 1;
        items.push({ url: s.url, title: s.title + '  \u2014  ' + s.url });
      });
      historyItems().forEach(function (h) {
        if (seen[h.url]) return;
        seen[h.url] = 1;
        items.push({ url: h.url, title: h.title + '  \u2014  ' + h.url });
      });
      items.forEach(function (it) {
        var d = div(el.list, 'ow-address-item');
        d.textContent = it.title;
        d.addEventListener('mousedown', function (e) {
          e.preventDefault();
        });
        d.addEventListener('click', function () {
          el.list.classList.remove('is-open');
          navigate(it.url);
        });
      });
      el.list.classList.add('is-open');
    }

    /* ------------------------------ actions ------------------------------ */
    function toolbarAction(action) {
      switch (action) {
        case 'back': goHistory(-1); break;
        case 'forward': goHistory(1); break;
        case 'stop': stopLoad(); break;
        case 'refresh': reload(); break;
        case 'home': navigate(Pages.HOME_URL); break;
        case 'search': navigate('http://www.seekfinder.com/'); break;
        case 'favorites': togglePanel('favorites'); break;
        case 'history': togglePanel('history'); break;
        case 'mail':
          WM.dialog({
            title: 'Outlook Express',
            icon: 'info',
            heading: 'Outlook Express is not installed',
            text: ['To use mail and news, ask your system administrator to install ' +
              'Outlook Express from the Windows 98 CD-ROM.'],
            width: 370
          });
          break;
        case 'print':
          WM.dialog({
            title: 'Print',
            icon: 'warn',
            heading: 'No printers are installed',
            text: ['Double-click Printers in Control Panel to add a printer before printing.'],
            width: 370
          });
          break;
      }
    }

    function onViewportClick(e) {
      var a = e.target.closest ? e.target.closest('a[href]') : null;
      if (a) {
        var href = a.getAttribute('href');
        e.preventDefault();
        if (/^mailto:/i.test(href)) {
          WM.dialog({
            title: 'Outlook Express',
            icon: 'info',
            heading: 'No mail client is installed',
            text: ['Cannot open ' + href.replace(/^mailto:/i, '') + '.'],
            width: 350
          });
          return;
        }
        navigate(href);
        return;
      }
      var buy = e.target.closest ? e.target.closest('[data-buy]') : null;
      if (buy) {
        WM.dialog({
          title: 'BargainBytes',
          icon: 'info',
          heading: 'Thank you for your order!',
          text: [
            'Item: ' + buy.getAttribute('data-buy'),
            'Order number: BB-' + Math.floor(100000 + Math.random() * 899999),
            'Please allow 6\u20138 weeks for delivery. Payment by check or money order.'
          ],
          width: 380
        });
        return;
      }
      var gb = e.target.closest ? e.target.closest('[data-guestbook]') : null;
      if (gb) guestbook();
    }

    function onViewportSubmit(e) {
      var form = e.target.closest ? e.target.closest('form[data-search]') : null;
      if (!form) return;
      e.preventDefault();
      var q = form.querySelector('[name="q"]');
      navigate('http://www.seekfinder.com/?q=' + encodeURIComponent(q ? q.value.trim() : ''));
    }

    function selectAll() {
      var sel = window.getSelection();
      sel.removeAllRanges();
      var range = document.createRange();
      range.selectNodeContents(el.viewport);
      sel.addRange(range);
    }

    function viewSource() {
      var entry = state.history[state.index];
      var url = entry ? entry.url : '';
      WM.dialog({
        title: 'Source of ' + url,
        width: 560,
        cancelValue: null,
        html:
          '<div class="dlg"><div class="dlg__text">' +
          '<pre class="src">' + esc(el.viewport.innerHTML) + '</pre>' +
          '</div></div>',
        buttons: [{ label: 'Close', value: 'ok', def: true }]
      });
    }

    function openLocation() {
      WM.dialog({
        title: 'Open',
        width: 430,
        cancelValue: null,
        html:
          '<div class="dlg"><div class="dlg__text">' +
          '<div class="dlg__row"><b>Type the Internet address of a document, and it ' +
          'will be opened for you.</b></div>' +
          '<div class="dlg__row"><input class="w98-input" data-loc spellcheck="false"></div>' +
          '</div></div>',
        buttons: [
          {
            label: 'OK',
            value: function (root) {
              var i = root.querySelector('[data-loc]');
              return i ? i.value.trim() : '';
            },
            def: true
          },
          { label: 'Cancel', value: null }
        ],
        onShow: function (root) {
          var i = root.querySelector('[data-loc]');
          i.value = el.input.value;
          i.focus();
          i.select();
        }
      }).then(function (v) {
        if (v) navigate(v);
      });
    }

    function doPrint() {
      toolbarAction('print');
    }

    function guestbook() {
      WM.dialog({
        title: "Dave's Guestbook",
        width: 430,
        cancelValue: null,
        html:
          '<div class="dlg"><div class="dlg__text">' +
          '<div class="dlg__row"><b>Sign my guestbook!!</b></div>' +
          '<div class="dlg__row"><input class="w98-input" data-gb-name placeholder="Your name" spellcheck="false"></div>' +
          '<div class="dlg__row"><textarea class="w98-input" data-gb-msg rows="3" ' +
          'placeholder="Your message"></textarea></div>' +
          '</div></div>',
        buttons: [
          {
            label: 'Sign It!',
            value: function (root) {
              var n = root.querySelector('[data-gb-name]');
              return (n && n.value.trim()) || 'Anonymous Surfer';
            },
            def: true
          },
          { label: 'Cancel', value: null }
        ],
        onShow: function (root) {
          root.querySelector('[data-gb-name]').focus();
        }
      }).then(function (name) {
        if (!name) return;
        WM.dialog({
          title: 'Guestbook',
          icon: 'info',
          heading: 'Thanks, ' + name + '!',
          text: ['Your message has been added to the guestbook. ' +
            'Dave will read it as soon as Mom is off the phone.'],
          width: 380
        });
      });
    }

    function internetOptions() {
      WM.dialog({
        title: 'Internet Options',
        icon: 'info',
        heading: 'Internet Options',
        text: [
          'Home page: ' + Pages.HOME_URL,
          'Temporary Internet files: 4.2 MB used',
          'Connection: Dial-Up Networking (14400 bps)',
          'Programs tab is unavailable in Web98.'
        ],
        width: 390
      });
    }

    function aboutIE() {
      WM.dialog({
        title: 'About Internet Explorer',
        width: 400,
        cancelValue: null,
        html:
          '<div class="dlg"><img class="dlg__icon" src="' + IE_ICON + '" alt="">' +
          '<div class="dlg__text">' +
          '<b>Microsoft (R) Internet Explorer 5</b>' +
          '<p>Version 5.00.0910.1308<br>' +
          'Cipher Strength: 40-bit<br>' +
          'Based on NCSA Mosaic. Distributed under a licensing agreement with Spyglass, Inc.<br>' +
          'Copyright (C) 1995-1999 Microsoft Corp.</p>' +
          '<p>This product is part of the Web98 desktop simulation.</p>' +
          '</div></div>',
        buttons: [{ label: 'OK', value: 'ok', def: true }]
      });
    }

    /* ------------------------------ bootstrap ---------------------------- */
    navigate(Pages.HOME_URL);

    api = {
      id: win.id,
      navigate: navigate,
      focus: function () { WM.focus(win.id); }
    };
    instances[win.id] = api;
    return api;
  }

  return { open: create };
})();
