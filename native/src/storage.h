#pragma once
// SQLite persistence compatible with the Electron Web98 schema
// (settings/history/bookmarks in <profile>/web98.db). Opening the existing
// file in place IS the data migration: no conversion, no reset.
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "json.h"

struct sqlite3;
struct sqlite3_stmt;

namespace web98 {

struct HistoryRow {
  int64_t id = 0;
  std::string url;
  std::string title;
  int64_t ts = 0;
};

struct BookmarkRow {
  int64_t id = 0;
  int64_t parent = 0;
  std::string name;
  std::string url;
  int64_t position = 0;
};

struct HistoryQuery {
  std::string q;
  int64_t since = 0;
  int64_t until = 0;
  int limit = 200;
};

class Storage {
 public:
  Storage() = default;
  ~Storage();

  Storage(const Storage&) = delete;
  Storage& operator=(const Storage&) = delete;

  // Opens (creating when missing) the DB at `path`. Returns false on error.
  bool open(const std::string& path, int historyLimit = 5000);
  void close();
  bool isOpen() const { return db_ != nullptr; }
  const std::string& path() const { return path_; }

  // --- settings (JSON values, same table as Electron) --------------------
  bool getSetting(const std::string& key, Json& out);
  void setSetting(const std::string& key, const Json& value);
  std::map<std::string, Json> allSettings();

  // --- history -------------------------------------------------------------
  void addHistory(const std::string& url, const std::string& title, int64_t ts);
  std::vector<HistoryRow> history(const HistoryQuery& q);
  void deleteHistory(int64_t id);
  void clearHistory();
  int64_t countHistory();
  void pruneHistory(int64_t maxRows);

  // --- bookmarks -----------------------------------------------------------
  BookmarkRow addBookmark(int64_t parent, const std::string& name,
                          const std::string& url, int64_t position);
  std::vector<BookmarkRow> listBookmarks();
  bool getBookmark(int64_t id, BookmarkRow& out);
  bool updateBookmark(int64_t id, const std::string& name, const std::string& url);
  void moveBookmark(int64_t id, int64_t parent, int64_t position);
  void deleteBookmarkTree(int64_t id);
  int64_t countBookmarks();

 private:
  bool exec(const char* sql);
  sqlite3_stmt* prep(const char* sql);

  sqlite3* db_ = nullptr;
  std::string path_;
  int historyLimit_ = 5000;
  int64_t historyCount_ = 0;

  sqlite3_stmt* stGetSetting_ = nullptr;
  sqlite3_stmt* stSetSetting_ = nullptr;
  sqlite3_stmt* stAllSettings_ = nullptr;
  sqlite3_stmt* stAddHistory_ = nullptr;
  sqlite3_stmt* stHistRecent_ = nullptr;
  sqlite3_stmt* stHistSearch_ = nullptr;
  sqlite3_stmt* stHistDay_ = nullptr;
  sqlite3_stmt* stDelHistory_ = nullptr;
  sqlite3_stmt* stClearHistory_ = nullptr;
  sqlite3_stmt* stCountHistory_ = nullptr;
  sqlite3_stmt* stPruneHistory_ = nullptr;
  sqlite3_stmt* stAddBookmark_ = nullptr;
  sqlite3_stmt* stListBookmarks_ = nullptr;
  sqlite3_stmt* stGetBookmark_ = nullptr;
  sqlite3_stmt* stRenameBookmark_ = nullptr;
  sqlite3_stmt* stMoveBookmark_ = nullptr;
  sqlite3_stmt* stDelTree_ = nullptr;
  sqlite3_stmt* stNextPos_ = nullptr;
  sqlite3_stmt* stCountBookmarks_ = nullptr;
};

}  // namespace web98
