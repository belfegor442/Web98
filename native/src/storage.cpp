#include "storage.h"

#include <sqlite3.h>

#include <algorithm>

namespace web98 {
namespace {

const char* kSchema =
    "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "url TEXT NOT NULL, title TEXT NOT NULL DEFAULT \"\", ts INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS history_ts ON history (ts DESC);"
    "CREATE INDEX IF NOT EXISTS history_url ON history (url);"
    "CREATE TABLE IF NOT EXISTS bookmarks (id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "parent INTEGER NOT NULL DEFAULT 0, name TEXT NOT NULL, "
    "url TEXT NOT NULL DEFAULT \"\", position INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX IF NOT EXISTS bookmarks_parent ON bookmarks (parent, position);";

std::string columnText(sqlite3_stmt* st, int col) {
  const unsigned char* p = sqlite3_column_text(st, col);
  if (!p) return std::string();
  return reinterpret_cast<const char*>(p);
}

}  // namespace

Storage::~Storage() { close(); }

bool Storage::exec(const char* sql) {
  char* err = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (err) sqlite3_free(err);
  return rc == SQLITE_OK;
}

sqlite3_stmt* Storage::prep(const char* sql) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return nullptr;
  return st;
}

bool Storage::open(const std::string& path, int historyLimit) {
  close();
  path_ = path;
  historyLimit_ = std::max(100, historyLimit);
  if (sqlite3_open_v2(path.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    db_ = nullptr;
    return false;
  }
  exec("PRAGMA journal_mode=WAL;");
  exec("PRAGMA synchronous=NORMAL;");
  if (!exec(kSchema)) {
    close();
    return false;
  }

  stGetSetting_ = prep("SELECT value FROM settings WHERE key = ?");
  stSetSetting_ =
      prep("INSERT INTO settings (key, value) VALUES (?, ?) "
           "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
  stAllSettings_ = prep("SELECT key, value FROM settings");
  stAddHistory_ = prep("INSERT INTO history (url, title, ts) VALUES (?, ?, ?)");
  stHistRecent_ =
      prep("SELECT id, url, title, ts FROM history ORDER BY ts DESC, id DESC LIMIT ?");
  stHistSearch_ =
      prep("SELECT id, url, title, ts FROM history WHERE url LIKE ? OR title LIKE ? "
           "ORDER BY ts DESC, id DESC LIMIT ?");
  stHistDay_ =
      prep("SELECT id, url, title, ts FROM history WHERE ts >= ? AND ts < ? "
           "ORDER BY ts DESC, id DESC LIMIT ?");
  stDelHistory_ = prep("DELETE FROM history WHERE id = ?");
  stClearHistory_ = prep("DELETE FROM history");
  stCountHistory_ = prep("SELECT COUNT(*) AS n FROM history");
  stPruneHistory_ =
      prep("DELETE FROM history WHERE id NOT IN "
           "(SELECT id FROM history ORDER BY ts DESC, id DESC LIMIT ?)");
  stAddBookmark_ =
      prep("INSERT INTO bookmarks (parent, name, url, position) VALUES (?, ?, ?, ?)");
  stListBookmarks_ =
      prep("SELECT id, parent, name, url, position FROM bookmarks "
           "ORDER BY parent, position, id");
  stGetBookmark_ =
      prep("SELECT id, parent, name, url, position FROM bookmarks WHERE id = ?");
  stRenameBookmark_ = prep("UPDATE bookmarks SET name = ?, url = ? WHERE id = ?");
  stMoveBookmark_ = prep("UPDATE bookmarks SET parent = ?, position = ? WHERE id = ?");
  stDelTree_ =
      prep("WITH RECURSIVE sub(id) AS ("
           "SELECT id FROM bookmarks WHERE id = ? "
           "UNION SELECT b.id FROM bookmarks b JOIN sub ON b.parent = sub.id"
           ") DELETE FROM bookmarks WHERE id IN (SELECT id FROM sub)");
  stNextPos_ =
      prep("SELECT COALESCE(MAX(position), -1) + 1 AS p FROM bookmarks WHERE parent = ?");
  stCountBookmarks_ = prep("SELECT COUNT(*) AS n FROM bookmarks");

  if (!stGetSetting_ || !stSetSetting_ || !stAddHistory_ || !stAddBookmark_) {
    close();
    return false;
  }
  historyCount_ = countHistory();
  return true;
}

void Storage::close() {
  auto drop = [](sqlite3_stmt*& st) {
    if (st) {
      sqlite3_finalize(st);
      st = nullptr;
    }
  };
  drop(stGetSetting_);
  drop(stSetSetting_);
  drop(stAllSettings_);
  drop(stAddHistory_);
  drop(stHistRecent_);
  drop(stHistSearch_);
  drop(stHistDay_);
  drop(stDelHistory_);
  drop(stClearHistory_);
  drop(stCountHistory_);
  drop(stPruneHistory_);
  drop(stAddBookmark_);
  drop(stListBookmarks_);
  drop(stGetBookmark_);
  drop(stRenameBookmark_);
  drop(stMoveBookmark_);
  drop(stDelTree_);
  drop(stNextPos_);
  drop(stCountBookmarks_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool Storage::getSetting(const std::string& key, Json& out) {
  if (!stGetSetting_) return false;
  sqlite3_reset(stGetSetting_);
  sqlite3_clear_bindings(stGetSetting_);
  sqlite3_bind_text(stGetSetting_, 1, key.c_str(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
  if (sqlite3_step(stGetSetting_) != SQLITE_ROW) return false;
  std::string text = columnText(stGetSetting_, 0);
  sqlite3_reset(stGetSetting_);
  return Json::parse(text, out);
}

void Storage::setSetting(const std::string& key, const Json& value) {
  if (!stSetSetting_) return;
  std::string text = value.dump();
  sqlite3_reset(stSetSetting_);
  sqlite3_clear_bindings(stSetSetting_);
  sqlite3_bind_text(stSetSetting_, 1, key.c_str(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stSetSetting_, 2, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
  sqlite3_step(stSetSetting_);
  sqlite3_reset(stSetSetting_);
}

std::map<std::string, Json> Storage::allSettings() {
  std::map<std::string, Json> out;
  if (!stAllSettings_) return out;
  sqlite3_reset(stAllSettings_);
  while (sqlite3_step(stAllSettings_) == SQLITE_ROW) {
    std::string k = columnText(stAllSettings_, 0);
    std::string v = columnText(stAllSettings_, 1);
    Json j;
    if (Json::parse(v, j)) out[k] = j;
  }
  sqlite3_reset(stAllSettings_);
  return out;
}

void Storage::addHistory(const std::string& url, const std::string& title, int64_t ts) {
  if (!stAddHistory_) return;
  sqlite3_reset(stAddHistory_);
  sqlite3_clear_bindings(stAddHistory_);
  sqlite3_bind_text(stAddHistory_, 1, url.c_str(), static_cast<int>(url.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stAddHistory_, 2, title.c_str(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
  sqlite3_bind_int64(stAddHistory_, 3, ts);
  sqlite3_step(stAddHistory_);
  sqlite3_reset(stAddHistory_);
  historyCount_++;
  if (historyCount_ > historyLimit_) {
    pruneHistory(historyLimit_);
    historyCount_ = historyLimit_;
  }
}

std::vector<HistoryRow> Storage::history(const HistoryQuery& q) {
  std::vector<HistoryRow> out;
  sqlite3_stmt* st = nullptr;
  if (q.since && q.until) {
    st = stHistDay_;
  } else if (!q.q.empty()) {
    st = stHistSearch_;
  } else {
    st = stHistRecent_;
  }
  if (!st) return out;
  sqlite3_reset(st);
  sqlite3_clear_bindings(st);
  int limit = std::max(1, std::min(q.limit, 500));
  if (st == stHistDay_) {
    sqlite3_bind_int64(st, 1, q.since);
    sqlite3_bind_int64(st, 2, q.until);
    sqlite3_bind_int(st, 3, limit);
  } else if (st == stHistSearch_) {
    std::string like = "%" + q.q + "%";
    sqlite3_bind_text(st, 1, like.c_str(), static_cast<int>(like.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, like.c_str(), static_cast<int>(like.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, limit);
  } else {
    sqlite3_bind_int(st, 1, limit);
  }
  while (sqlite3_step(st) == SQLITE_ROW) {
    HistoryRow r;
    r.id = sqlite3_column_int64(st, 0);
    r.url = columnText(st, 1);
    r.title = columnText(st, 2);
    r.ts = sqlite3_column_int64(st, 3);
    out.push_back(std::move(r));
  }
  sqlite3_reset(st);
  return out;
}

void Storage::deleteHistory(int64_t id) {
  if (!stDelHistory_) return;
  sqlite3_reset(stDelHistory_);
  sqlite3_bind_int64(stDelHistory_, 1, id);
  if (sqlite3_step(stDelHistory_) == SQLITE_DONE && historyCount_ > 0) historyCount_--;
  sqlite3_reset(stDelHistory_);
}

void Storage::clearHistory() {
  if (!stClearHistory_) return;
  sqlite3_step(stClearHistory_);
  sqlite3_reset(stClearHistory_);
  historyCount_ = 0;
}

int64_t Storage::countHistory() {
  if (!stCountHistory_) return 0;
  sqlite3_reset(stCountHistory_);
  int64_t n = 0;
  if (sqlite3_step(stCountHistory_) == SQLITE_ROW) n = sqlite3_column_int64(stCountHistory_, 0);
  sqlite3_reset(stCountHistory_);
  return n;
}

void Storage::pruneHistory(int64_t maxRows) {
  if (!stPruneHistory_) return;
  sqlite3_reset(stPruneHistory_);
  sqlite3_bind_int64(stPruneHistory_, 1, maxRows);
  sqlite3_step(stPruneHistory_);
  sqlite3_reset(stPruneHistory_);
  historyCount_ = countHistory();
}

BookmarkRow Storage::addBookmark(int64_t parent, const std::string& name,
                                 const std::string& url, int64_t position) {
  BookmarkRow row;
  if (!stAddBookmark_) return row;
  if (position < 0 && stNextPos_) {
    sqlite3_reset(stNextPos_);
    sqlite3_bind_int64(stNextPos_, 1, parent);
    if (sqlite3_step(stNextPos_) == SQLITE_ROW) position = sqlite3_column_int64(stNextPos_, 0);
    sqlite3_reset(stNextPos_);
    if (position < 0) position = 0;
  }
  sqlite3_reset(stAddBookmark_);
  sqlite3_bind_int64(stAddBookmark_, 1, parent);
  sqlite3_bind_text(stAddBookmark_, 2, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stAddBookmark_, 3, url.c_str(), static_cast<int>(url.size()), SQLITE_TRANSIENT);
  sqlite3_bind_int64(stAddBookmark_, 4, position);
  sqlite3_step(stAddBookmark_);
  sqlite3_reset(stAddBookmark_);
  int64_t id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
  getBookmark(id, row);
  return row;
}

std::vector<BookmarkRow> Storage::listBookmarks() {
  std::vector<BookmarkRow> out;
  if (!stListBookmarks_) return out;
  sqlite3_reset(stListBookmarks_);
  while (sqlite3_step(stListBookmarks_) == SQLITE_ROW) {
    BookmarkRow r;
    r.id = sqlite3_column_int64(stListBookmarks_, 0);
    r.parent = sqlite3_column_int64(stListBookmarks_, 1);
    r.name = columnText(stListBookmarks_, 2);
    r.url = columnText(stListBookmarks_, 3);
    r.position = sqlite3_column_int64(stListBookmarks_, 4);
    out.push_back(std::move(r));
  }
  sqlite3_reset(stListBookmarks_);
  return out;
}

bool Storage::getBookmark(int64_t id, BookmarkRow& out) {
  if (!stGetBookmark_) return false;
  sqlite3_reset(stGetBookmark_);
  sqlite3_bind_int64(stGetBookmark_, 1, id);
  bool ok = sqlite3_step(stGetBookmark_) == SQLITE_ROW;
  if (ok) {
    out.id = sqlite3_column_int64(stGetBookmark_, 0);
    out.parent = sqlite3_column_int64(stGetBookmark_, 1);
    out.name = columnText(stGetBookmark_, 2);
    out.url = columnText(stGetBookmark_, 3);
    out.position = sqlite3_column_int64(stGetBookmark_, 4);
  }
  sqlite3_reset(stGetBookmark_);
  return ok;
}

bool Storage::updateBookmark(int64_t id, const std::string& name, const std::string& url) {
  if (!stRenameBookmark_) return false;
  sqlite3_reset(stRenameBookmark_);
  sqlite3_bind_text(stRenameBookmark_, 1, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stRenameBookmark_, 2, url.c_str(), static_cast<int>(url.size()), SQLITE_TRANSIENT);
  sqlite3_bind_int64(stRenameBookmark_, 3, id);
  bool ok = sqlite3_step(stRenameBookmark_) == SQLITE_DONE;
  sqlite3_reset(stRenameBookmark_);
  return ok;
}

void Storage::moveBookmark(int64_t id, int64_t parent, int64_t position) {
  if (!stMoveBookmark_) return;
  sqlite3_reset(stMoveBookmark_);
  sqlite3_bind_int64(stMoveBookmark_, 1, parent);
  sqlite3_bind_int64(stMoveBookmark_, 2, position);
  sqlite3_bind_int64(stMoveBookmark_, 3, id);
  sqlite3_step(stMoveBookmark_);
  sqlite3_reset(stMoveBookmark_);
}

void Storage::deleteBookmarkTree(int64_t id) {
  if (!stDelTree_) return;
  sqlite3_reset(stDelTree_);
  sqlite3_bind_int64(stDelTree_, 1, id);
  sqlite3_step(stDelTree_);
  sqlite3_reset(stDelTree_);
}

int64_t Storage::countBookmarks() {
  if (!stCountBookmarks_) return 0;
  sqlite3_reset(stCountBookmarks_);
  int64_t n = 0;
  if (sqlite3_step(stCountBookmarks_) == SQLITE_ROW) n = sqlite3_column_int64(stCountBookmarks_, 0);
  sqlite3_reset(stCountBookmarks_);
  return n;
}

}  // namespace web98
