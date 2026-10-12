// Web98 native unit tests — pure logic only (no CEF).
// Mirrors test/unit/runner.js coverage for the C++ ports:
// json / safeurl / urlbar / settings / storage / security / dlpath.
// Build: web98_unit.exe (linked by native/CMakeLists.txt). Exit code 0 = pass.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "base.h"
#include "dlpath.h"
#include "json.h"
#include "safeurl.h"
#include "security.h"
#include "settings.h"
#include "storage.h"
#include "urlbar.h"

namespace {

int g_passed = 0;
int g_failed = 0;

void pass() { ++g_passed; }
void fail(const char* name, const std::string& why) {
  ++g_failed;
  std::fprintf(stderr, "  FAIL  %s — %s\n", name, why.c_str());
}

#define CHECK(name, cond)                        \
  do {                                           \
    if (cond) {                                  \
      pass();                                    \
    } else {                                     \
      fail(name, "check failed: " #cond);        \
    }                                            \
  } while (0)

#define CHECK_EQ_STR(name, actual, expected)                          \
  do {                                                                \
    std::string a_ = (actual);                                        \
    std::string e_ = (expected);                                      \
    if (a_ == e_) {                                                   \
      pass();                                                         \
    } else {                                                          \
      fail(name, "expected '" + e_ + "', got '" + a_ + "'");          \
    }                                                                 \
  } while (0)

#define CHECK_EQ_INT(name, actual, expected)                          \
  do {                                                                \
    long long a_ = static_cast<long long>(actual);                    \
    long long e_ = static_cast<long long>(expected);                  \
    if (a_ == e_) {                                                   \
      pass();                                                         \
    } else {                                                          \
      char buf[128];                                                  \
      std::snprintf(buf, sizeof(buf), "expected %lld, got %lld", e_,  \
                    a_);                                              \
      fail(name, buf);                                                \
    }                                                                 \
  } while (0)

using namespace web98;

/* --------------------------------- json ---------------------------------- */

void runJson() {
  const char* n = "json";
  Json j;
  CHECK(n, Json::parse("{\"a\":1,\"b\":[true,null,\"x\"],\"c\":{\"d\":2.5}}", j));
  CHECK_EQ_STR(n, j.dump(), "{\"a\":1,\"b\":[true,null,\"x\"],\"c\":{\"d\":2.5}}");
  const Json* a = j.find("a");
  CHECK(n, a && a->isNumber() && a->num == 1);
  const Json* b = j.find("b");
  CHECK(n, b && b->isArray() && b->arr.size() == 3);
  CHECK_EQ_STR(n, b->arr[2].stringOr("?"), "x");
  const Json* c = j.find("c");
  CHECK(n, c && c->isObject());
  const Json* d = c ? c->find("d") : nullptr;
  CHECK(n, d && d->num == 2.5);

  Json bad;
  CHECK(n, !Json::parse("{", bad));
  CHECK(n, !Json::parse("", bad));
  CHECK(n, Json::parse("null", bad) && bad.isNull());
  CHECK(n, Json::parse("\"hi\"", bad) && bad.str == "hi");
  CHECK(n, Json::parse("true", bad) && bad.b);
  CHECK(n, Json::parse("-3.5e2", bad) && bad.num == -350);

  // Escape round-trip.
  Json s = Json::makeString("line\n\"q\"\t\\");
  CHECK(n, Json::parse(s.dump(), bad) && bad.str == s.str);
}

/* -------------------------------- safeurl -------------------------------- */

void runSafeUrl() {
  const char* n = "safeurl";
  CHECK_EQ_STR(n, schemeOf("HTTPS://x"), "https:");
  CHECK_EQ_STR(n, schemeOf("no-scheme"), "");
  CHECK_EQ_STR(n, schemeOf(std::string(kMaxUrlLen + 1, 'a')), "");

  CHECK(n, isLoadableUrl("http://example.com"));
  CHECK(n, isLoadableUrl("https://example.com"));
  CHECK(n, isLoadableUrl("file:///C:/x.txt"));
  CHECK(n, isLoadableUrl("about:blank"));
  CHECK(n, !isLoadableUrl("javascript:alert(1)"));
  CHECK(n, isLoadableUrl("data:text/html,x"));
  CHECK(n, !isLoadableUrl("chrome://settings"));
  CHECK(n, !isLoadableUrl(""));

  // Page-initiated navigation from a web origin: file: blocked.
  CHECK(n, isAllowedNavigation("https://ok.example", true));
  CHECK(n, !isAllowedNavigation("file:///C:/secret", true));
  CHECK(n, !isAllowedNavigation("javascript:1", true));
  // From a local page (chrome/tab on file:) file: is allowed.
  CHECK(n, isAllowedNavigation("https://ok.example", false));
  CHECK(n, isAllowedNavigation("file:///x", false));

  CHECK(n, isWebUrl("http://a"));
  CHECK(n, !isWebUrl("file:///a"));
  CHECK(n, isSafeExternalUrl("mailto:a@b.c"));
  CHECK(n, isSafeExternalUrl("tel:+15551212"));
  CHECK(n, !isSafeExternalUrl("https://x"));
  CHECK(n, isBookmarkUrl("ftp://x"));
  CHECK(n, !isBookmarkUrl("javascript:1"));
  CHECK(n, isSearchTemplate("https://ddg.com/?q=%s"));
  CHECK(n, isSearchTemplate("https://ddg.com/?q="));

  auto kept = sanitizeSessionList({"https://a", "javascript:1", "http://b"}, 10);
  CHECK_EQ_INT(n, kept.size(), 2);
  CHECK_EQ_STR(n, kept[0], "https://a");
  auto capped = sanitizeSessionList({"https://a", "https://b", "https://c"}, 2);
  CHECK_EQ_INT(n, capped.size(), 2);
}

/* --------------------------------- urlbar -------------------------------- */

void runUrlbar() {
  const char* n = "urlbar";
  const std::string ddg = "https://duckduckgo.com/?q=%s";

  CHECK(n, parseAddress("", ddg).type == AddressType::Empty);
  CHECK(n, parseAddress("   ", ddg).type == AddressType::Empty);

  AddressResult r = parseAddress("https://example.com/a?b=c", ddg);
  CHECK(n, r.type == AddressType::Url);
  CHECK_EQ_STR(n, r.url, "https://example.com/a?b=c");

  r = parseAddress("example.com", ddg);
  CHECK(n, r.type == AddressType::Url);
  CHECK_EQ_STR(n, r.url, "http://example.com");

  r = parseAddress("example.com:8080/path?q=1", ddg);
  CHECK_EQ_STR(n, r.url, "http://example.com:8080/path?q=1");

  r = parseAddress("localhost:3000", ddg);
  CHECK_EQ_STR(n, r.url, "http://localhost:3000");

  r = parseAddress("192.168.1.10", ddg);
  CHECK_EQ_STR(n, r.url, "http://192.168.1.10");

  r = parseAddress("how to search things", ddg);
  CHECK(n, r.type == AddressType::Search);
  CHECK_EQ_STR(n, r.url, "https://duckduckgo.com/?q=how%20to%20search%20things");

  r = parseAddress("a b&c=d", ddg);
  CHECK_EQ_STR(n, r.url, "https://duckduckgo.com/?q=a%20b%26c%3Dd");

  r = parseAddress("javascript:document.title=1", ddg);
  CHECK(n, r.type == AddressType::Javascript);
  CHECK_EQ_STR(n, r.url, "document.title=1");

  r = parseAddress("view-source:https://example.com", ddg);
  CHECK(n, r.type == AddressType::Url);
  CHECK_EQ_STR(n, r.url, "view-source:https://example.com");

  r = parseAddress("mailto:a@b.c", ddg);
  CHECK_EQ_STR(n, r.url, "mailto:a@b.c");

  r = parseAddress("about:blank", ddg);
  CHECK(n, r.type == AddressType::Url);

  // Template without %s appends.
  r = parseAddress("x", "https://s.example/?q=");
  CHECK_EQ_STR(n, r.url, "https://s.example/?q=x");

  CHECK_EQ_STR(n, encodeURIComponent("a b&c=d"), "a%20b%26c%3Dd");
  CHECK_EQ_STR(n, buildSearchUrl(ddg, "cat dog"),
               "https://duckduckgo.com/?q=cat%20dog");
}

/* -------------------------------- settings ------------------------------- */

void runSettings() {
  const char* n = "settings";
  SettingsModel m;
  m.initEmpty();
  CHECK(n, m.all().isObject());
  // Defaults must include a working search template.
  CHECK(n, !searchTemplateFor(m.all()).empty());

  CHECK_EQ_STR(n, sanitizeText("  hi\x01\x02  ", 100), "hi");
  CHECK_EQ_INT(n, sanitizeText(std::string(300, 'a'), 10).size(), 10);
  CHECK(n, isGrantOrigin("https://example.com"));
  CHECK(n, !isGrantOrigin("not a url"));
  CHECK(n, isPermissionName("geolocation"));
  CHECK(n, !isPermissionName("../../etc"));

  CHECK(n, clampNumber(5, 0, 1, 0) == 1);
  CHECK(n, clampNumber(-1, 0, 1, 0) == 0);
  CHECK(n, clampNumber(0.4, 0, 1, 0) == 0.4);
  CHECK(n, clampNumber(9, 0, 1, 0.5) == 1);  // out-of-range clamps to hi

  int64_t id = 0;
  CHECK(n, sanitizeId(Json::makeNumber(3), id) && id == 3);
  CHECK(n, !sanitizeId(Json::makeNumber(-1), id));
  CHECK(n, sanitizeId(Json::makeString("3"), id) && id == 3);  // Number("3")=3 in JS

  BookmarkIn b;
  CHECK(n, sanitizeBookmark(
               [] {
                 Json o = Json::makeObject();
                 o.obj.emplace_back("name", Json::makeString("Site"));
                 o.obj.emplace_back("url", Json::makeString("https://x/"));
                 o.obj.emplace_back("parent", Json::makeNumber(0));
                 return o;
               }(),
               b));
  CHECK_EQ_STR(n, b.name, "Site");
  CHECK(n, !sanitizeBookmark(Json::makeNull(), b));

  Json patch = Json::makeObject();
  patch.obj.emplace_back("zoom", Json::makeNumber(1.5));
  patch.obj.emplace_back("evil", Json::makeString("x"));
  Json out;
  std::vector<std::string> rejected;
  CHECK(n, sanitizeSettingsPatchValue("zoom", Json::makeNumber(99), out, nullptr));
  CHECK(n, out.isNumber() && out.num <= 3.0);  // clamped
  CHECK(n, !sanitizeSettingsPatchValue("evil", Json::makeString("x"), out,
                                       &rejected));
  CHECK_EQ_INT(n, rejected.size(), 1);
  (void)patch;

  // Model get/set roundtrip.
  m.setString("homepage", "https://example.org/");
  m.setBool("sessionRestore", false);
  m.setNumber("zoom", 1.25);
  CHECK_EQ_STR(n, m.getString("homepage", ""), "https://example.org/");
  CHECK(n, !m.getBool("sessionRestore", true));
  CHECK(n, m.getNumber("zoom", 0) == 1.25);
  CHECK_EQ_STR(n, m.getString("missing", "fb"), "fb");

  // Model's engines include builtins after initEmpty.
  Json eng = m.get("engines");
  CHECK(n, eng.isArray() && !eng.arr.empty());
}

/* -------------------------------- storage -------------------------------- */

void runStorage() {
  const char* n = "storage";
  wchar_t tmp[MAX_PATH];
  ::GetTempPathW(MAX_PATH, tmp);
  std::string dir = toUtf8(tmp) + "web98-native-unit";
  ensureDirTree(dir);
  std::string db = pathJoin(dir, "unit.db");
  ::DeleteFileA(db.c_str());

  Storage s;
  CHECK(n, s.open(db));
  CHECK(n, s.isOpen());

  s.setSetting("homepage", Json::makeString("https://example.org/"));
  s.setSetting("zoom", Json::makeNumber(1.5));
  Json got;
  CHECK(n, s.getSetting("homepage", got) && got.str == "https://example.org/");
  CHECK(n, s.getSetting("zoom", got) && got.num == 1.5);
  CHECK(n, !s.getSetting("missing", got));
  auto all = s.allSettings();
  CHECK_EQ_INT(n, all.size(), 2);

  s.addHistory("https://a.example/", "A", 1000);
  s.addHistory("https://b.example/", "B", 2000);
  s.addHistory("https://c.example/", "C", 3000);
  HistoryQuery q;
  q.limit = 10;
  auto rows = s.history(q);
  CHECK_EQ_INT(n, rows.size(), 3);
  CHECK_EQ_STR(n, rows[0].title, "C");  // newest first

  q.q = "b.example";
  rows = s.history(q);
  CHECK_EQ_INT(n, rows.size(), 1);
  CHECK_EQ_STR(n, rows[0].title, "B");

  q.q.clear();
  s.deleteHistory(rows[0].id);
  CHECK_EQ_INT(n, s.countHistory(), 2);

  for (int i = 0; i < 30; i++) {
    s.addHistory("https://p" + std::to_string(i) + ".example/",
                 "P" + std::to_string(i), 4000 + i);
  }
  s.pruneHistory(5);
  CHECK_EQ_INT(n, s.countHistory(), 5);
  q.limit = 5;
  rows = s.history(q);
  CHECK_EQ_STR(n, rows[0].title, "P29");

  s.clearHistory();
  CHECK_EQ_INT(n, s.countHistory(), 0);

  BookmarkRow folder = s.addBookmark(0, "Work", "", 0);
  CHECK(n, folder.id > 0);
  s.addBookmark(folder.id, "First", "https://1.example/", 0);
  s.addBookmark(folder.id, "Second", "https://2.example/", 0);
  s.addBookmark(0, "Root item", "https://root.example/", 0);
  auto bm = s.listBookmarks();
  CHECK_EQ_INT(n, bm.size(), 4);
  CHECK_EQ_STR(n, bm[0].name, "Work");
  CHECK_EQ_STR(n, bm[2].name, "First");

  BookmarkRow first = bm[2];
  CHECK(n, s.updateBookmark(first.id, "First (renamed)", first.url));
  BookmarkRow again;
  CHECK(n, s.getBookmark(first.id, again));
  CHECK_EQ_STR(n, again.name, "First (renamed)");

  // Recursive folder delete.
  s.deleteBookmarkTree(folder.id);
  bm = s.listBookmarks();
  CHECK_EQ_INT(n, bm.size(), 1);
  CHECK_EQ_STR(n, bm[0].name, "Root item");

  // Quick-access (speed dial) pin/unpin + list.
  BookmarkRow a = s.addBookmark(0, "Alpha", "https://a.example/", -1);
  BookmarkRow b = s.addBookmark(0, "Beta", "https://b.example/", -1);
  s.addBookmark(0, "Folder", "", -1);  // folders never appear in quick list
  CHECK_EQ_INT(n, (int)s.listQuickBookmarks().size(), 0);
  bool quick = true;
  CHECK(n, s.toggleBookmarkQuick(a.id, &quick));
  CHECK(n, quick);
  quick = true;
  CHECK(n, s.toggleBookmarkQuick(b.id, &quick));
  CHECK(n, quick);
  auto qrows = s.listQuickBookmarks();
  CHECK_EQ_INT(n, (int)qrows.size(), 2);
  CHECK_EQ_STR(n, qrows[0].name, "Alpha");
  CHECK_EQ_STR(n, qrows[1].name, "Beta");
  // Toggle off removes from quick list; toggling twice is idempotent.
  CHECK(n, s.toggleBookmarkQuick(a.id, &quick));
  CHECK(n, !quick);
  CHECK_EQ_INT(n, (int)s.listQuickBookmarks().size(), 1);
  BookmarkRow aRow;
  CHECK(n, s.getBookmark(a.id, aRow));
  CHECK(n, !aRow.quick);

  // Persistence across reopen.
  s.close();
  Storage s2;
  CHECK(n, s2.open(db));
  CHECK_EQ_INT(n, s2.countHistory(), 0);
  CHECK_EQ_INT(n, s2.countBookmarks(), 4);
  auto q2 = s2.listQuickBookmarks();
  CHECK_EQ_INT(n, (int)q2.size(), 1);
  CHECK_EQ_STR(n, q2[0].name, "Beta");
  s2.close();
  ::DeleteFileA(db.c_str());
}

/* -------------------------------- security ------------------------------- */

void runSecurity() {
  const char* n = "security";
  CertGrants g;
  CHECK(n, !g.allows("https://bad.example"));
  g.grant("https://bad.example");
  CHECK(n, g.allows("https://bad.example"));
  CHECK(n, !g.allows("https://other.example"));

  CHECK_EQ_STR(n, classifyLoadError(-200, ""), "cert");
  CHECK_EQ_STR(n, classifyLoadError(-201, ""), "cert");
  CHECK_EQ_STR(n, classifyLoadError(-105, ""), "net");  // name not resolved
  CHECK_EQ_STR(n, classifyLoadError(-102, ""), "net");

  // Default: fullscreen allowed, everything else denied.
  Json perms = Json::makeObject();
  CHECK(n, permissionAllowed(perms, "https://x", "fullscreen"));
  CHECK(n, !permissionAllowed(perms, "https://x", "geolocation"));

  Json origin = Json::makeObject();
  origin.obj.emplace_back("geolocation", Json::makeBool(true));
  perms.obj.emplace_back("https://x", origin);
  CHECK(n, permissionAllowed(perms, "https://x", "geolocation"));
  CHECK(n, !permissionAllowed(perms, "https://y", "geolocation"));
}

/* --------------------------------- dlpath -------------------------------- */

void runDlpath() {
  const char* n = "dlpath";
  CHECK_EQ_STR(n, sanitizeFilename("../../evil.exe"), "evil.exe");
  CHECK_EQ_STR(n, sanitizeFilename("a<b>c:d|e"), "a_b_c_d_e");
  CHECK_EQ_STR(n, sanitizeFilename("CON"), "_CON");
  CHECK_EQ_STR(n, sanitizeFilename("trailing. "), "trailing");
  CHECK_EQ_INT(n, sanitizeFilename(std::string(200, 'x')).size() <= 120, 1);
  CHECK_EQ_STR(n, sanitizeFilename(""), "download");

  wchar_t tmp[MAX_PATH];
  ::GetTempPathW(MAX_PATH, tmp);
  std::string dir = toUtf8(tmp) + "web98-dlunit";
  ensureDirTree(dir);
  ::DeleteFileA(pathJoin(dir, "file.txt").c_str());
  ::DeleteFileA(pathJoin(dir, "file (2).txt").c_str());
  ::DeleteFileA(pathJoin(dir, "file (3).txt").c_str());
  std::string p1 = uniquePath(dir, "file.txt");
  CHECK_EQ_STR(n, p1, pathJoin(dir, "file.txt"));
  CHECK(n, writeFileAtomic(p1, "x"));
  std::string p2 = uniquePath(dir, "file.txt");
  CHECK_EQ_STR(n, p2, pathJoin(dir, "file (2).txt"));

  // Inside candidate is kept as-is.
  CHECK_EQ_STR(n, containPath(dir, pathJoin(dir, "sub/x.txt")),
               pathJoin(dir, "sub/x.txt"));
  // Escaping candidate falls back to dir/basename.
  CHECK_EQ_STR(n, containPath(dir, "C:\\Windows\\system32\\evil.dll"),
               pathJoin(dir, "evil.dll"));

  std::string full = resolveDownloadPath(dir, "../weird name.html");
  CHECK(n, startsWith(full, dir));

  ::DeleteFileA(p1.c_str());
  ::DeleteFileA(p2.c_str());
  ::DeleteFileA(full.c_str());
}

}  // namespace

int main() {
  std::printf("web98 native unit tests\n");
  runJson();
  runSafeUrl();
  runUrlbar();
  runSettings();
  runStorage();
  runSecurity();
  runDlpath();
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
