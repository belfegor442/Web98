'use strict';

/**
 * SQLite persistence for Web98 (settings, history, bookmarks, sessions).
 * Uses better-sqlite3 (synchronous, small, no server process).
 * The DB path is injectable so unit tests can use a temp file.
 */

var Database = require('better-sqlite3');

var SCHEMA = [
  'CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)',
  'CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY AUTOINCREMENT, url TEXT NOT NULL, title TEXT NOT NULL DEFAULT "", ts INTEGER NOT NULL)',
  'CREATE INDEX IF NOT EXISTS history_ts ON history (ts DESC)',
  'CREATE INDEX IF NOT EXISTS history_url ON history (url)',
  'CREATE TABLE IF NOT EXISTS bookmarks (id INTEGER PRIMARY KEY AUTOINCREMENT, parent INTEGER NOT NULL DEFAULT 0, name TEXT NOT NULL, url TEXT NOT NULL DEFAULT "", position INTEGER NOT NULL DEFAULT 0)',
  'CREATE INDEX IF NOT EXISTS bookmarks_parent ON bookmarks (parent, position)'
];

function openStore(dbPath) {
  var db = new Database(dbPath);
  db.pragma('journal_mode = WAL');
  db.exec(SCHEMA.join(';\n'));

  var stmts = {
    getSetting: db.prepare('SELECT value FROM settings WHERE key = ?'),
    setSetting: db.prepare(
      'INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value'
    ),
    allSettings: db.prepare('SELECT key, value FROM settings'),
    delSetting: db.prepare('DELETE FROM settings WHERE key = ?'),

    addHistory: db.prepare('INSERT INTO history (url, title, ts) VALUES (?, ?, ?)'),
    historyRecent: db.prepare(
      'SELECT id, url, title, ts FROM history ORDER BY ts DESC, id DESC LIMIT ?'
    ),
    historySearch: db.prepare(
      "SELECT id, url, title, ts FROM history WHERE url LIKE ? OR title LIKE ? ORDER BY ts DESC, id DESC LIMIT ?"
    ),
    historyByDay: db.prepare(
      'SELECT id, url, title, ts FROM history WHERE ts >= ? AND ts < ? ORDER BY ts DESC, id DESC LIMIT ?'
    ),
    delHistory: db.prepare('DELETE FROM history WHERE id = ?'),
    clearHistory: db.prepare('DELETE FROM history'),
    countHistory: db.prepare('SELECT COUNT(*) AS n FROM history'),
    pruneHistory: db.prepare(
      'DELETE FROM history WHERE id NOT IN (SELECT id FROM history ORDER BY ts DESC, id DESC LIMIT ?)'
    ),

    addBookmark: db.prepare(
      'INSERT INTO bookmarks (parent, name, url, position) VALUES (?, ?, ?, ?)'
    ),
    listBookmarks: db.prepare(
      'SELECT id, parent, name, url, position FROM bookmarks ORDER BY parent, position, id'
    ),
    getBookmark: db.prepare('SELECT id, parent, name, url, position FROM bookmarks WHERE id = ?'),
    renameBookmark: db.prepare('UPDATE bookmarks SET name = ?, url = ? WHERE id = ?'),
    moveBookmark: db.prepare('UPDATE bookmarks SET parent = ?, position = ? WHERE id = ?'),
    delBookmark: db.prepare('DELETE FROM bookmarks WHERE id = ?'),
    delBookmarkTree: db.prepare(
      'DELETE FROM bookmarks WHERE id = @id OR parent IN (SELECT id FROM bookmarks WHERE parent = @id)'
    ),
    nextBookmarkPos: db.prepare(
      'SELECT COALESCE(MAX(position), -1) + 1 AS p FROM bookmarks WHERE parent = ?'
    ),
    countBookmarks: db.prepare('SELECT COUNT(*) AS n FROM bookmarks')
  };

  var api = {
    db: db,

    /* ------------------------------ settings ----------------------------- */
    getSetting: function (key, fallback) {
      var row = stmts.getSetting.get(key);
      if (!row) return fallback;
      try {
        return JSON.parse(row.value);
      } catch (e) {
        return fallback;
      }
    },
    setSetting: function (key, value) {
      stmts.setSetting.run(key, JSON.stringify(value));
    },
    allSettings: function () {
      var out = {};
      stmts.allSettings.all().forEach(function (r) {
        try {
          out[r.key] = JSON.parse(r.value);
        } catch (e) {
          /* skip corrupt row */
        }
      });
      return out;
    },
    delSetting: function (key) {
      stmts.delSetting.run(key);
    },

    /* ------------------------------ history ------------------------------ */
    addHistory: function (url, title, ts) {
      stmts.addHistory.run(url, title || '', ts || Date.now());
    },
    history: function (opts) {
      opts = opts || {};
      var limit = opts.limit || 200;
      if (opts.since && opts.until) {
        return stmts.historyByDay.all(opts.since, opts.until, limit);
      }
      if (opts.q) {
        var like = '%' + opts.q + '%';
        return stmts.historySearch.all(like, like, limit);
      }
      return stmts.historyRecent.all(limit);
    },
    deleteHistory: function (id) {
      stmts.delHistory.run(id);
    },
    clearHistory: function () {
      stmts.clearHistory.run();
    },
    countHistory: function () {
      return stmts.countHistory.get().n;
    },
    pruneHistory: function (maxRows) {
      stmts.pruneHistory.run(maxRows);
    },

    /* ------------------------------ bookmarks ---------------------------- */
    addBookmark: function (b) {
      var pos = stmts.nextBookmarkPos.get(b.parent || 0).p;
      var info = stmts.addBookmark.run(
        b.parent || 0,
        b.name || b.url,
        b.url || '',
        b.position == null ? pos : b.position
      );
      return getBookmark(info.lastInsertRowid);
    },
    listBookmarks: function () {
      return stmts.listBookmarks.all();
    },
    getBookmark: getBookmark,
    updateBookmark: function (id, fields) {
      var b = getBookmark(id);
      if (!b) return null;
      stmts.renameBookmark.run(
        fields.name != null ? fields.name : b.name,
        fields.url != null ? fields.url : b.url,
        id
      );
      return getBookmark(id);
    },
    moveBookmark: function (id, parent, position) {
      stmts.moveBookmark.run(parent, position, id);
    },
    deleteBookmark: function (id) {
      stmts.delBookmark.run(id);
    },
    countBookmarks: function () {
      return stmts.countBookmarks.get().n;
    },

    close: function () {
      db.close();
    }
  };

  function getBookmark(id) {
    return stmts.getBookmark.get(id) || null;
  }

  return api;
}

module.exports = { openStore: openStore };
