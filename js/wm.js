/*
 * Web98 window manager — draggable/resizable windows, taskbar buttons,
 * focus/z-order, minimize/maximize/restore and modal-style dialogs.
 * No modules: plain global `WM` so the page also works from file://.
 */
(function () {
  'use strict';

  var desktop = document.getElementById('desktop');
  var tasksEl = document.getElementById('tasks');

  var seq = 0;
  var zRegular = 20;
  var zDialog = 3000;
  var windows = new Map();

  function make(tag, cls, parent) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (parent) parent.appendChild(n);
    return n;
  }

  function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  function activeId() {
    var best = null;
    windows.forEach(function (w) {
      if (w.minimized) return;
      if (!best || parseInt(w.root.style.zIndex || 0, 10) > parseInt(best.root.style.zIndex || 0, 10)) {
        best = w;
      }
    });
    return best ? best.id : null;
  }

  function focus(id) {
    var win = windows.get(id);
    if (!win) return;
    if (win.minimized) restore(id);
    win.root.style.zIndex = String(win.dialog ? (zDialog += 1) : (zRegular += 1));
    windows.forEach(function (w) {
      var on = w.id === id;
      w.root.classList.toggle('is-active', on);
      if (w.taskBtn) w.taskBtn.classList.toggle('is-active', on && !w.minimized);
    });
  }

  function minimize(id) {
    var win = windows.get(id);
    if (!win || win.minimized) return;
    win.minimized = true;
    win.root.classList.add('is-minimized');
    if (win.taskBtn) win.taskBtn.classList.remove('is-active');
    var next = activeId();
    if (next) focus(next);
  }

  function restore(id) {
    var win = windows.get(id);
    if (!win) return;
    win.minimized = false;
    win.root.classList.remove('is-minimized');
  }

  function toggleMaximize(id) {
    var win = windows.get(id);
    if (!win) return;
    var r = win.root;
    if (win.maximized) {
      win.maximized = false;
      r.classList.remove('is-max');
      var p = win.rect || { x: 40, y: 30, w: 480, h: 360 };
      r.style.left = p.x + 'px';
      r.style.top = p.y + 'px';
      r.style.width = p.w + 'px';
      r.style.height = p.h + 'px';
    } else {
      win.rect = {
        x: r.offsetLeft,
        y: r.offsetTop,
        w: r.offsetWidth,
        h: r.offsetHeight
      };
      win.maximized = true;
      r.classList.add('is-max');
      r.style.left = '0px';
      r.style.top = '0px';
      r.style.width = '100%';
      r.style.height = '100%';
    }
    focus(id);
    if (win.onResize) win.onResize();
  }

  function close(id) {
    var win = windows.get(id);
    if (!win) return;
    win.root.remove();
    if (win.taskBtn) win.taskBtn.remove();
    windows.delete(id);
    if (win.onClose) win.onClose();
    var next = activeId();
    if (next) focus(next);
  }

  /* ------------------------------ dragging ------------------------------- */
  function drag(win, startX, startY) {
    var r = win.root;
    var ox = r.offsetLeft;
    var oy = r.offsetTop;
    var dw = desktop.clientWidth;
    var dh = desktop.clientHeight;

    function move(e) {
      var nx = ox + (e.clientX - startX);
      var ny = oy + (e.clientY - startY);
      r.style.left = clamp(nx, 40 - r.offsetWidth, dw - 40) + 'px';
      r.style.top = clamp(ny, 0, dh - 24) + 'px';
    }
    function up() {
      document.removeEventListener('mousemove', move);
      document.removeEventListener('mouseup', up);
      document.body.style.userSelect = '';
    }
    document.body.style.userSelect = 'none';
    document.addEventListener('mousemove', move);
    document.addEventListener('mouseup', up);
  }

  function resize(win, startX, startY) {
    var r = win.root;
    var ow = r.offsetWidth;
    var oh = r.offsetHeight;
    var maxW = desktop.clientWidth - r.offsetLeft;
    var maxH = desktop.clientHeight - r.offsetTop;

    function move(e) {
      r.style.width = clamp(ow + (e.clientX - startX), 200, Math.max(200, maxW)) + 'px';
      r.style.height = clamp(oh + (e.clientY - startY), 130, Math.max(130, maxH)) + 'px';
      if (win.onResize) win.onResize();
    }
    function up() {
      document.removeEventListener('mousemove', move);
      document.removeEventListener('mouseup', up);
      document.body.style.userSelect = '';
    }
    document.body.style.userSelect = 'none';
    document.addEventListener('mousemove', move);
    document.addEventListener('mouseup', up);
  }

  /* ------------------------------ create --------------------------------- */
  function open(opts) {
    opts = opts || {};
    var id = opts.id || 'win' + (++seq);
    if (windows.has(id)) {
      focus(id);
      return windows.get(id);
    }

    var root = make('div', 'ow-root win', desktop);
    if (opts.className) root.className += ' ' + opts.className;
    root.dataset.winId = id;

    var tb = make('div', 'ow-titlebar', root);
    var icon = make('div', 'ow-titlebar__icon', tb);
    if (opts.icon) icon.style.backgroundImage = 'url("' + opts.icon + '")';
    else icon.style.display = 'none';
    var titleEl = make('div', 'ow-titlebar__text', tb);
    titleEl.textContent = opts.title || 'Window';

    var controls = make('div', 'ow-titlebar__controls', tb);
    var isDialog = !!opts.dialog;
    if (!isDialog) addWinBtn(controls, 'minimize');
    if (!isDialog && opts.resizable !== false) addWinBtn(controls, 'maximize');
    addWinBtn(controls, 'close');

    var body = make('div', 'win-body', root);

    var dw = desktop.clientWidth;
    var dh = desktop.clientHeight;
    var w = Math.min(opts.width || 480, dw - 10);
    var h = Math.min(opts.height || 360, dh - 10);
    var x, y;
    if (isDialog || opts.center) {
      x = Math.round((dw - w) / 2);
      y = Math.round(Math.max(6, (dh - h) / 2 - 30));
    } else {
      x = opts.x != null ? opts.x : Math.min(60 + seq * 26, Math.max(4, dw - w - 8));
      y = opts.y != null ? opts.y : Math.min(40 + seq * 26, Math.max(4, dh - h - 8));
    }
    root.style.width = w + 'px';
    root.style.height = h + 'px';
    root.style.left = x + 'px';
    root.style.top = y + 'px';

    var win = {
      id: id,
      root: root,
      body: body,
      titleEl: titleEl,
      dialog: isDialog,
      minimized: false,
      maximized: false,
      rect: { x: x, y: y, w: w, h: h },
      taskBtn: null,
      onClose: opts.onClose || null,
      onResize: opts.onResize || null
    };
    windows.set(id, win);

    /* taskbar button */
    if (opts.taskbar !== false) {
      var btn = make('button', 'taskbtn', tasksEl);
      btn.type = 'button';
      var img = make('img', '', btn);
      if (opts.icon) img.src = opts.icon;
      else img.style.display = 'none';
      var span = make('span', '', btn);
      span.textContent = opts.title || 'Window';
      btn.title = opts.title || '';
      btn.addEventListener('click', function () {
        if (win.minimized) {
          restore(id);
          focus(id);
        } else if (win.root.classList.contains('is-active')) {
          minimize(id);
        } else {
          focus(id);
        }
      });
      win.taskBtn = btn;
    }

    /* controls */
    controls.addEventListener('click', function (e) {
      var b = e.target.closest('.ow-winbtn');
      if (!b) return;
      var c = b.dataset.control;
      if (c === 'close') close(id);
      else if (c === 'minimize') minimize(id);
      else if (c === 'maximize') toggleMaximize(id);
    });

    /* focus + drag */
    root.addEventListener('mousedown', function () {
      if (!root.classList.contains('is-active')) focus(id);
    });
    tb.addEventListener('mousedown', function (e) {
      if (e.target.closest('.ow-winbtn')) return;
      if (win.maximized) return;
      if (e.button !== 0) return;
      drag(win, e.clientX, e.clientY);
    });
    tb.addEventListener('dblclick', function (e) {
      if (e.target.closest('.ow-winbtn')) return;
      if (opts.resizable !== false) toggleMaximize(id);
    });

    /* resize grip */
    if (!isDialog && opts.resizable !== false) {
      var grip = make('div', 'win-resize', root);
      grip.title = 'Resize';
      grip.addEventListener('mousedown', function (e) {
        if (win.maximized) return;
        e.preventDefault();
        e.stopPropagation();
        focus(id);
        resize(win, e.clientX, e.clientY);
      });
    }

    focus(id);
    if (opts.maximized) toggleMaximize(id);
    return win;
  }

  function addWinBtn(parent, control) {
    var b = make('button', 'ow-winbtn', parent);
    b.type = 'button';
    b.dataset.control = control;
    b.title = control.charAt(0).toUpperCase() + control.slice(1);
    return b;
  }

  /* ------------------------------ dialogs -------------------------------- */
  function dialog(opts) {
    opts = opts || {};
    return new Promise(function (resolve) {
      var settled = false;
      var buttons = opts.buttons || [{ label: 'OK', value: 'ok', def: true }];
      var cancelValue = opts.cancelValue !== undefined ? opts.cancelValue : null;

      var w = open({
        id: opts.id || null,
        title: opts.title || '',
        icon: null,
        width: opts.width || 340,
        height: 10, // measured after content insert
        dialog: true,
        taskbar: false,
        center: true
      });

      var dlg = make('div', '', w.body);

      if (opts.html) {
        dlg.innerHTML = opts.html;
      } else {
        var top = make('div', 'dlg', dlg);
        if (opts.icon) {
          var im = make('img', 'dlg__icon', top);
          im.src = 'assets/icons/' + opts.icon + '.svg';
          im.alt = '';
        }
        var txt = make('div', 'dlg__text', top);
        if (opts.heading) {
          var hd = make('b', '', txt);
          hd.textContent = opts.heading;
        }
        var lines = Array.isArray(opts.text) ? opts.text : [opts.text || ''];
        lines.forEach(function (line, i) {
          var p = make('p', '', txt);
          p.textContent = line;
          if (i === lines.length - 1) p.style.marginBottom = '0';
        });
        if (opts.html2) {
          var wrap = make('div', 'dlg__text', top);
          wrap.innerHTML = opts.html2;
        }
      }

      var row = make('div', 'dlg__actions' + (opts.centerButtons ? '' : ' is-right'), dlg);
      buttons.forEach(function (b) {
        var el = make('button', 'w98-btn' + (b.def ? ' is-default' : ''), row);
        el.type = 'button';
        el.textContent = b.label;
        el.addEventListener('click', function () {
          finish(typeof b.value === 'function' ? b.value(dlg) : b.value);
        });
      });

      if (opts.onShow) opts.onShow(dlg, w);

      function finish(value) {
        if (settled) return;
        settled = true;
        document.removeEventListener('keydown', onKey, true);
        w.onClose = null;
        close(w.id);
        resolve(value);
      }

      function onKey(e) {
        if (e.key === 'Escape') {
          e.stopPropagation();
          finish(cancelValue);
        } else if (e.key === 'Enter') {
          var def = buttons.filter(function (b) { return b.def; })[0] || buttons[0];
          if (def && !(e.target && e.target.tagName === 'TEXTAREA')) {
            e.preventDefault();
            finish(typeof def.value === 'function' ? def.value(dlg) : def.value);
          }
        }
      }
      document.addEventListener('keydown', onKey, true);

      w.onClose = function () {
        if (settled) return;
        settled = true;
        document.removeEventListener('keydown', onKey, true);
        resolve(cancelValue);
      };

      /* fit height to content */
      requestAnimationFrame(function () {
        var h = w.root.scrollHeight;
        var maxH = desktop.clientHeight - 8;
        w.root.style.height = Math.min(h, maxH) + 'px';
        var y = Math.round(Math.max(4, (desktop.clientHeight - w.root.offsetHeight) / 2 - 20));
        w.root.style.top = y + 'px';
        w.root.style.left =
          Math.round((desktop.clientWidth - w.root.offsetWidth) / 2) + 'px';
        /* focus default button, unless onShow already focused a field */
        var ae = document.activeElement;
        var fieldFocused = ae && dlg.contains(ae) &&
          /^(input|textarea|select)$/i.test(ae.tagName);
        if (!fieldFocused) {
          var defBtn = row.querySelector('.is-default') || row.querySelector('button');
          if (defBtn) defBtn.focus();
        }
      });
    });
  }

  window.WM = {
    open: open,
    close: close,
    focus: focus,
    minimize: minimize,
    restore: restore,
    toggleMaximize: toggleMaximize,
    dialog: dialog,
    setTitle: function (id, text) {
      var win = windows.get(id);
      if (!win) return;
      win.titleEl.textContent = text;
      if (win.taskBtn) {
        win.taskBtn.querySelector('span').textContent = text;
        win.taskBtn.title = text;
      }
    },
    list: function () { return windows; }
  };
})();
