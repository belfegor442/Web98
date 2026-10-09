/* global window, document */
'use strict';

/**
 * Shared helpers for Web98 dialog windows: IPC bridge, titlebar wiring,
 * payload channel, list selection utilities, button/keyboard conventions.
 */

(function () {
  var api = window.web98;

  function invoke(ch, data) { return api.invoke(ch, data); }
  function send(ch, data) { api.send(ch, data); }

  var dlg = {
    invoke: invoke,
    send: send,
    on: function (ch, cb) { return api.on(ch, cb); },
    payload: function (cb) { return api.on('payload', cb); },
    close: function () { return invoke('win:close'); },

    /* titlebar close button + active/inactive window styling */
    init: function () {
      var bar = document.querySelector('.ow-titlebar');
      if (bar) {
        bar.addEventListener('click', function (e) {
          var ctrl = e.target.closest('[data-control]');
          if (!ctrl) return;
          var c = ctrl.getAttribute('data-control');
          if (c === 'close' || c === 'minimize') invoke('win:close');
        });
      }
      api.on('win:active', function (s) {
        var root = document.querySelector('.ow-root.win');
        if (root) root.classList.toggle('is-active', !!s.active);
      });
      document.addEventListener('keydown', function (e) {
        if (e.key === 'Escape') dlg.close();
        if (e.key === 'Enter' && e.target && e.target.tagName === 'INPUT' && e.target.type === 'text') {
          var def = document.querySelector('.dlg-foot .is-default');
          if (def) def.click();
        }
      });
    },

    /* select-to-highlight rows in a .w98-list; cb(rowEl, item) on double click */
    bindList: function (listEl, opts) {
      opts = opts || {};
      var selected = null;
      function select(row) {
        if (selected) selected.classList.remove('is-sel');
        selected = row;
        if (row) row.classList.add('is-sel');
        if (opts.onSelect) opts.onSelect(row ? row._item : null);
      }
      listEl.addEventListener('mousedown', function (e) {
        var row = e.target.closest('.row');
        if (!row) { select(null); return; }
        select(row);
      });
      listEl.addEventListener('dblclick', function (e) {
        var row = e.target.closest('.row');
        if (row && opts.onOpen) opts.onOpen(row._item);
      });
      return {
        select: function (item) {
          var rows = listEl.querySelectorAll('.row');
          for (var i = 0; i < rows.length; i++) {
            if (rows[i]._item === item) { select(rows[i]); return; }
          }
          select(null);
        },
        clear: function () { select(null); },
        selected: function () { return selected ? selected._item : null; }
      };
    },

    bytes: function (n) {
      if (!n && n !== 0) return '';
      if (n < 1024) return n + ' B';
      if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
      if (n < 1073741824) return (n / 1048576).toFixed(1) + ' MB';
      return (n / 1073741824).toFixed(2) + ' GB';
    },
    time: function (ts) {
      var d = new Date(ts);
      function p(n) { return n < 10 ? '0' + n : '' + n; }
      return p(d.getMonth() + 1) + '/' + p(d.getDate()) + '/' + d.getFullYear() +
        ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
    },
    escapeHtml: function (s) {
      return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
        return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
      });
    }
  };

  window.dlg = dlg;
  document.addEventListener('DOMContentLoaded', function () {
    dlg.init();
  });
})();
