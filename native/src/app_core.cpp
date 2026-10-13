#include "app_core.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "base.h"
#include "cef_engine.h"
#include "cef_util.h"
#include "dialogs.h"
#include "dlpath.h"
#include "menus.h"
#include "safeurl.h"
#include "settings.h"
#include "urlbar.h"
#include "window.h"

namespace web98 {
namespace {

// Matches ipc.js COMMAND_NAME_RE / CONTEXT_COMMAND_RE.
bool commandNameOk(const std::string& s) {
  if (s.size() < 3 || s.size() > 64) return false;
  size_t colon = s.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size())
    return false;
  auto lower = [](char c) { return c >= 'A' && c <= 'Z'; };
  if (lower(s[0])) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (i == colon) continue;
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool contextCommandOk(const std::string& s) {
  if (s.empty() || s.size() > 32) return false;
  if (s[0] < 'a' || s[0] > 'z') return false;
  for (char c : s) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}

const char* kDialogPages[] = {"about",  "downloads", "favorites",
                              "history", "settings", "source"};

Json bookmarkToJson(const BookmarkRow& b) {
  Json o = Json::makeObject();
  o.obj.emplace_back("id", Json::makeNumber(static_cast<double>(b.id)));
  o.obj.emplace_back("parent", Json::makeNumber(static_cast<double>(b.parent)));
  o.obj.emplace_back("name", Json::makeString(b.name));
  o.obj.emplace_back("url", Json::makeString(b.url));
  o.obj.emplace_back("position", Json::makeNumber(static_cast<double>(b.position)));
  o.obj.emplace_back("quick", Json::makeBool(b.quick));
  return o;
}

Json historyToJson(const HistoryRow& h) {
  Json o = Json::makeObject();
  o.obj.emplace_back("id", Json::makeNumber(static_cast<double>(h.id)));
  o.obj.emplace_back("url", Json::makeString(h.url));
  o.obj.emplace_back("title", Json::makeString(h.title));
  o.obj.emplace_back("ts", Json::makeNumber(static_cast<double>(h.ts)));
  return o;
}

Json downloadToJson(const DownloadRec& d) {
  Json o = Json::makeObject();
  o.obj.emplace_back("id", Json::makeNumber(d.id));
  o.obj.emplace_back("filename", Json::makeString(d.filename));
  o.obj.emplace_back("savePath", Json::makeString(d.savePath));
  o.obj.emplace_back("url", Json::makeString(d.url));
  o.obj.emplace_back("state", Json::makeString(d.state));
  o.obj.emplace_back("received", Json::makeNumber(static_cast<double>(d.received)));
  o.obj.emplace_back("total", Json::makeNumber(static_cast<double>(d.total)));
  o.obj.emplace_back("startTime",
                     Json::makeNumber(static_cast<double>(d.startTime)));
  o.obj.emplace_back("endTime", Json::makeNumber(static_cast<double>(d.endTime)));
  return o;
}

// Electron `new URL(url).origin`; ""/unparsable -> "null".
std::string safeOrigin(const std::string& url) {
  std::string scheme = schemeOf(url);
  if (scheme.empty()) return "null";
  size_t start = scheme.size();
  if (url.size() < start + 3 || url.compare(start, 3, "://") != 0) return "null";
  size_t hostEnd = url.find_first_of("/?#", start + 3);
  std::string origin =
      hostEnd == std::string::npos ? url : url.substr(0, hostEnd);
  if (!origin.empty() && origin.back() != '/' &&
      origin.find('/', 3) == std::string::npos) {
    // origin built from scheme + authority only
  }
  // Strip any path that was included (scheme://host only).
  size_t auth = origin.find("://");
  if (auth != std::string::npos) {
    size_t slash = origin.find('/', auth + 3);
    if (slash != std::string::npos) origin.resize(slash);
  }
  return origin.empty() ? "null" : origin;
}

}  // namespace

AppCore& AppCore::get() {
  static AppCore instance;
  return instance;
}

/* ------------------------------- lifecycle -------------------------------- */

bool AppCore::init() {
  startupAtMs_ = nowMs();
  ensureDirTree(userDataDir());
  std::string dbPath = pathJoin(userDataDir(), "web98.db");
  if (!storage_.open(dbPath)) {
    std::fprintf(stderr, "[web98] cannot open profile db: %s\n",
                 dbPath.c_str());
    return false;
  }
  settings_.initEmpty();
  for (const auto& kv : storage_.allSettings()) settings_.setRaw(kv.first, kv.second);

  downloads_.setDownloadsDir(settings_.getString("downloadsDir", ""));
  downloads_.setListener(this);
  permissions_ = settings_.get("permissions");
  if (!permissions_.isObject()) permissions_ = Json::makeObject();
  return true;
}

void AppCore::createInitialWindows() {
  std::vector<SessionWindow> restore;
  if (settings_.getBool("sessionRestore", true)) {
    restore = parseStoredSession(settings_.get("lastSession"));
  }
  if (restore.empty()) {
    mainWindows_.push_back(std::make_unique<MainWindow>());
    MainWindow* w = mainWindows_.back().get();
    w->create(nullptr, true);
    return;
  }
  for (const auto& sw : restore) {
    mainWindows_.push_back(std::make_unique<MainWindow>());
    MainWindow* w = mainWindows_.back().get();
    w->create(&sw, true);
  }
}

void AppCore::saveSession(MainWindow* closing) {
  if (!settings_.getBool("sessionRestore", true)) return;
  std::vector<MainWindow*> live;
  for (auto& w : mainWindows_)
    if (w) live.push_back(w.get());
  // A window the user just closed must not come back on the next launch —
  // unless it is the only one, in which case its close is the session.
  if (closing) {
    auto it = std::find(live.begin(), live.end(), closing);
    if (it != live.end()) {
      std::vector<MainWindow*> others;
      for (MainWindow* w : live)
        if (w != closing) others.push_back(w);
      if (!others.empty()) live = others;
    }
  }
  if (live.empty()) return;  // keep the previously stored session
  std::vector<SessionWindow> out;
  for (MainWindow* w : live) {
    TabManager* t = w->tabs();
    if (!t) continue;
    SessionWindow sw;
    t->each([&](Tab* tab) {
      if (!tab || tab->url.empty() || tab->url == "about:blank") return;
      if (t->active() == tab) sw.active = static_cast<int>(sw.urls.size());
      sw.urls.push_back(tab->url);
    });
    if (!sw.urls.empty()) out.push_back(std::move(sw));
  }
  if (out.empty()) return;  // keep the previously stored session
  Json session = buildStoredSession(out);
  if (session.isNull()) return;
  settings_.setRaw("lastSession", session);
  storage_.setSetting("lastSession", session);
}

void AppCore::retireMainWindow(MainWindow* w) {
  for (auto it = mainWindows_.begin(); it != mainWindows_.end(); ++it) {
    if (it->get() == w) {
      retired_.push_back(std::move(*it));
      mainWindows_.erase(it);
      break;
    }
  }
  // Deferred destroy: the WndProc that called us is still on the stack.
  PostUi([this] { retired_.clear(); });
  onLastWindowClosed();
}

void AppCore::onLastWindowClosed() {
  // Electron window-all-closed counts every BrowserWindow, dialogs included.
  if (mainWindows_.empty() && dialogs_.empty() && !quitting_) {
    ::PostQuitMessage(0);
  }
}

void AppCore::shutdown() {
  // Destroy every CEF-backed window while the context is still alive;
  // after CefShutdown the unique_ptr destructors must not touch CEF.
  // The ~MainWindow/~DialogWindow destructors Detach() + CloseBrowser().
  saveSession();
  quitting_ = true;
  retired_.clear();
  retiredDialogs_.clear();
  dialogs_.clear();
  mainWindows_.clear();
}

void AppCore::quit() {
  quitting_ = true;
  saveSession();
  // Copy raw pointers first: requestClose() runs synchronously and retires
  // windows from mainWindows_, invalidating range iterators.
  std::vector<MainWindow*> toClose;
  for (auto& w : mainWindows_)
    if (w) toClose.push_back(w.get());
  for (MainWindow* w : toClose) w->requestClose();
  // Close any open dialogs so they don't keep the process alive.
  std::vector<DialogWindow*> dialogs;
  for (auto& d : dialogs_)
    if (d) dialogs.push_back(d.get());
  for (DialogWindow* d : dialogs) d->requestClose();
  if (mainWindows_.empty()) ::PostQuitMessage(0);
}

void AppCore::closeAllMainWindows() {
  std::vector<MainWindow*> toClose;
  for (auto& w : mainWindows_)
    if (w) toClose.push_back(w.get());
  for (MainWindow* w : toClose) w->requestClose();
}

/* ------------------------------- services ---------------------------------- */

void AppCore::setSetting(const std::string& key, const Json& value) {
  Json sanitized;
  std::vector<std::string> rejected;
  if (!sanitizeSettingsPatchValue(key, value, sanitized, &rejected)) return;
  settings_.setRaw(key, sanitized);
  storage_.setSetting(key, sanitized);
  if (key == "downloadsDir") {
    downloads_.setDownloadsDir(sanitized.isString() ? sanitized.str : "");
  }
  if (key == "permissions") permissions_ = sanitized;
  broadcast("settings:changed", settings_.all());
}

Json AppCore::applySettingsPatch(const Json& patch) {
  if (!patch.isObject()) return settings_.all();
  std::vector<std::string> rejected;
  for (const auto& kv : patch.obj) {
    Json out;
    if (sanitizeSettingsPatchValue(kv.first, kv.second, out, &rejected)) {
      settings_.setRaw(kv.first, out);
      storage_.setSetting(kv.first, out);
      if (kv.first == "downloadsDir") {
        downloads_.setDownloadsDir(out.isString() ? out.str : "");
      }
      if (kv.first == "permissions") permissions_ = out;
    }
  }
  Json out = settings_.all();
  if (!rejected.empty()) {
    Json arr = Json::makeArray();
    for (const auto& r : rejected) arr.arr.push_back(Json::makeString(r));
    out.obj.emplace_back("rejected", std::move(arr));
  }
  broadcast("settings:changed", settings_.all());
  return out;
}

bool AppCore::permissionAllowed(const std::string& origin,
                                const std::string& permission) {
  return web98::permissionAllowed(permissions_, origin, permission);
}

void AppCore::listGrants(Json& out) const {
  Json arr = Json::makeArray();
  if (!permissions_.isObject()) {
    out = std::move(arr);
    return;
  }
  for (const auto& byOrigin : permissions_.obj) {
    if (!byOrigin.second.isObject()) continue;
    for (const auto& p : byOrigin.second.obj) {
      if (!p.second.isBool()) continue;
      Json o = Json::makeObject();
      o.obj.emplace_back("origin", Json::makeString(byOrigin.first));
      o.obj.emplace_back("permission", Json::makeString(p.first));
      o.obj.emplace_back("value", Json::makeBool(p.second.b));
      arr.arr.push_back(std::move(o));
    }
  }
  out = std::move(arr);
}

void AppCore::setGrant(const std::string& origin, const std::string& permission,
                       const Json& value) {
  if (!isGrantOrigin(origin) || !isPermissionName(permission)) return;
  if (!value.isNull() && !value.isBool()) return;
  Json perms = permissions_.isObject() ? permissions_ : Json::makeObject();
  Json byOrigin;
  const Json* existing = perms.find(origin);
  if (existing && existing->isObject()) byOrigin = *existing;
  else byOrigin = Json::makeObject();
  if (value.isNull()) {
    for (auto it = byOrigin.obj.begin(); it != byOrigin.obj.end();) {
      if (it->first == permission) it = byOrigin.obj.erase(it);
      else ++it;
    }
  } else {
    bool found = false;
    for (auto& kv : byOrigin.obj) {
      if (kv.first == permission) {
        kv.second = value;
        found = true;
        break;
      }
    }
    if (!found) byOrigin.obj.emplace_back(permission, value);
  }
  bool originEmpty = byOrigin.obj.empty();
  for (auto it = perms.obj.begin(); it != perms.obj.end();) {
    if (it->first == origin) it = perms.obj.erase(it);
    else ++it;
  }
  if (!originEmpty) perms.obj.emplace_back(origin, std::move(byOrigin));
  permissions_ = perms;
  settings_.setRaw("permissions", perms);
  storage_.setSetting("permissions", perms);
  broadcast("settings:changed", settings_.all());
}

/* ------------------------------ windows ------------------------------------ */

MainWindow* AppCore::createMainWindow(bool show) {
  mainWindows_.push_back(std::make_unique<MainWindow>());
  MainWindow* w = mainWindows_.back().get();
  w->create(nullptr, show);
  return w;
}

MainWindow* AppCore::primaryWindow() {
  return mainWindows_.empty() ? nullptr : mainWindows_.front().get();
}

bool AppCore::openDialog(const std::string& page, MainWindow* owner,
                         const Json* payload) {
  bool known = false;
  for (const char* p : kDialogPages)
    if (page == p) known = true;
  if (!known) return false;

  // Electron keeps a single window per page and re-focuses it.
  for (auto& d : dialogs_) {
    if (d && d->page() == page && d->hwnd() &&
        ::IsWindow(d->hwnd())) {
      if (payload) d->emit("payload", *payload);
      ::SetForegroundWindow(d->hwnd());
      return true;
    }
  }
  auto win = std::make_unique<DialogWindow>();
  DialogWindow* raw = win.get();
  if (!raw->create(owner, page, payload)) return false;
  dialogs_.push_back(std::move(win));
  return true;
}

void AppCore::removeDialog(DialogWindow* d) {
  for (auto it = dialogs_.begin(); it != dialogs_.end(); ++it) {
    if (it->get() == d) {
      retiredDialogs_.push_back(std::move(*it));
      dialogs_.erase(it);
      break;
    }
  }
  // Deferred destroy: the WndProc that called us is still on the stack.
  PostUi([this] { retiredDialogs_.clear(); });
  onLastWindowClosed();
}

void AppCore::broadcast(const std::string& channel, const Json& payload) {
  std::vector<MainWindow*> windows;
  for (auto& w : mainWindows_)
    if (w) windows.push_back(w.get());
  for (auto* w : windows) w->emit(channel, payload);
  std::vector<DialogWindow*> dialogs;
  for (auto& d : dialogs_)
    if (d) dialogs.push_back(d.get());
  for (auto* d : dialogs) d->emit(channel, payload);
}

void AppCore::OnDownloadsChanged() {
  Json arr = Json::makeArray();
  for (const auto& d : downloads_.list()) arr.arr.push_back(downloadToJson(d));
  broadcast("downloads", arr);
}

/* --------------------------------- IPC -------------------------------------- */

void AppCore::replyError(int64_t id, CefRefPtr<CefFrame> frame,
                         const std::string& message) {
  Json err = Json::makeObject();
  err.obj.emplace_back("message", Json::makeString(message));
  SendIpcReply(frame, id, false, err);
}

void AppCore::handleIpc(const BrowserCtx& ctx, const std::string& channel,
                        const Json& arg, bool isInvoke, int64_t id,
                        CefRefPtr<CefFrame> frame) {
  auto needTabs = [&]() -> TabManager* {
    if (ctx.tabs) return ctx.tabs;
    if (ctx.window) return ctx.window->tabs();
    return nullptr;
  };
  auto isChrome = [&]() -> bool { return ctx.role == BrowserCtx::kChrome; };
  auto isDialog = [&]() -> bool { return ctx.role == BrowserCtx::kDialog; };
  auto isTab = [&]() -> bool { return ctx.role == BrowserCtx::kTab; };
  auto reply = [&](const Json& v) { SendIpcReply(frame, id, true, v); };
  auto replyNull = [&]() { SendIpcReply(frame, id, true, Json::makeNull()); };

  // ---- window ----
  if (channel == "win:min") {
    if (ctx.window) ctx.window->minimize();
    if (ctx.dialog) ctx.dialog->requestClose();
    if (isInvoke) replyNull();
    return;
  }
  if (channel == "win:max") {
    bool now = false;
    if (ctx.window) now = ctx.window->toggleMaximize();
    if (isInvoke) reply(Json::makeBool(now));
    return;
  }
  if (channel == "win:close") {
    if (ctx.window) ctx.window->requestClose();
    else if (ctx.dialog) ctx.dialog->requestClose();
    else if (ctx.tabs && ctx.tabs->window()) ctx.tabs->window()->requestClose();
    if (isInvoke) replyNull();
    return;
  }
  if (channel == "win:title") {
    if (ctx.window) {
      ctx.window->setTitle(sanitizeText(arg.stringOr(""), 200).empty()
                               ? "Web98"
                               : sanitizeText(arg.stringOr(""), 200));
    }
    if (isInvoke) replyNull();
    return;
  }
  if (channel == "win:isMax") {
    bool m = ctx.window ? ctx.window->isMaximized() : false;
    if (isInvoke) reply(Json::makeBool(m));
    return;
  }
  // Frameless drag via the standard move loop. A second request within
  // 500 ms with almost no cursor travel is a double-click -> toggle zoom.
  if (channel == "win:drag") {
    HWND hwnd = nullptr;
    if (ctx.window) hwnd = ctx.window->hwnd();
    else if (ctx.dialog) hwnd = ctx.dialog->hwnd();
    if (hwnd) {
      POINT pt;
      ::GetCursorPos(&pt);
      int64_t now = nowMs();
      static int64_t lastDragMs = 0;
      static POINT lastDragPt = {0, 0};
      bool dbl = now - lastDragMs <= 500 &&
                 std::abs(pt.x - lastDragPt.x) <= 4 &&
                 std::abs(pt.y - lastDragPt.y) <= 4;
      lastDragMs = now;
      lastDragPt = pt;
      if (dbl) {
        if (ctx.window) ctx.window->toggleMaximize();
      } else {
        ::ReleaseCapture();
        ::SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
    }
    return;
  }

  // ---- tabs ----
  if (channel == "tabs:list") {
    TabManager* t = needTabs();
    if (!t) {
      if (isInvoke) replyError(id, frame, "No browser window for this request");
      return;
    }
    reply(t->list());
    return;
  }
  if (channel == "tabs:create") {
    TabManager* t = needTabs();
    if (!t) {
      if (isInvoke) replyError(id, frame, "No browser window for this request");
      return;
    }
    std::string url = arg.isObject() && arg.find("url") ? arg.find("url")->stringOr("") : "";
    if (!url.empty() && !isLoadableUrl(url)) {
      replyError(id, frame, "Refusing to load URL");
      return;
    }
    Tab* tab = t->create(url, true);
    reply(Json::makeNumber(tab ? tab->id : 0));
    return;
  }
  if (channel == "tabs:close" || channel == "tabs:activate") {
    TabManager* t = needTabs();
    int64_t tid = 0;
    const Json* idv = arg.isObject() ? arg.find("id") : nullptr;
    bool ok = idv && sanitizeId(*idv, tid);
    if (t && ok) {
      if (channel == "tabs:close") t->close(static_cast<int>(tid));
      else t->setActive(static_cast<int>(tid));
    }
    if (isInvoke) replyNull();
    return;
  }

  // ---- navigation ----
  if (channel == "nav:go") {
    TabManager* t = needTabs();
    if (!t) {
      if (isInvoke) replyError(id, frame, "No browser window for this request");
      return;
    }
    std::string address;
    if (arg.isObject()) {
      const Json* a = arg.find("address");
      if (a && a->isString()) address = a->str.substr(0, kMaxUrlLen);
    }
    AddressResult parsed = parseAddress(
        address, searchTemplateFor(settings_.all()), "http");
    if (parsed.type == AddressType::Empty) {
      Json emptyReply = Json::makeObject();
      emptyReply.obj.emplace_back("ok", Json::makeBool(false));
      reply(std::move(emptyReply));
      return;
    }
    if (parsed.type == AddressType::Javascript) {
      Json o = Json::makeObject();
      o.obj.emplace_back("ok", Json::makeBool(false));
      o.obj.emplace_back("blocked", Json::makeBool(true));
      reply(std::move(o));
      return;
    }
    if (!isLoadableUrl(parsed.url)) {
      Json o = Json::makeObject();
      o.obj.emplace_back("ok", Json::makeBool(false));
      o.obj.emplace_back("blocked", Json::makeBool(true));
      reply(std::move(o));
      return;
    }
    t->load(t->active(), parsed.url);
    Json o = Json::makeObject();
    o.obj.emplace_back("ok", Json::makeBool(true));
    o.obj.emplace_back("url", Json::makeString(parsed.url));
    reply(std::move(o));
    return;
  }
  if (channel == "nav:back" || channel == "nav:forward" ||
      channel == "nav:reload" || channel == "nav:stop" ||
      channel == "nav:home") {
    TabManager* t = needTabs();
    Tab* tab = t ? t->active() : nullptr;
    if (tab && tab->client && tab->client->browser()) {
      CefRefPtr<CefBrowser> br = tab->client->browser();
      if (channel == "nav:back" && br->CanGoBack()) br->GoBack();
      else if (channel == "nav:forward" && br->CanGoForward()) br->GoForward();
      else if (channel == "nav:reload") br->Reload();
      else if (channel == "nav:stop") br->StopLoad();
      else if (channel == "nav:home")
        t->load(tab, settings_.getString("homepage", homepageUrl()));
    }
    if (isInvoke) replyNull();
    return;
  }
  if (channel == "app:find") {
    TabManager* t = needTabs();
    Tab* tab = t ? t->active() : nullptr;
    std::string action = arg.isObject() && arg.find("action")
                             ? arg.find("action")->stringOr("")
                             : "";
    std::string term = sanitizeText(arg.isObject() && arg.find("term")
                                        ? arg.find("term")->stringOr("")
                                        : "",
                                    200);
    if (tab && tab->client && tab->client->browser()) {
      CefRefPtr<CefBrowserHost> host = tab->client->browser()->GetHost();
      if (action == "stop") {
        host->StopFinding(true);
      } else if (!term.empty()) {
        bool forward = action != "prev";
        if (action == "next") host->Find(term, true, false, true);
        else if (action == "prev") host->Find(term, false, false, true);
        else host->Find(term, forward, false, false);
      }
    }
    if (isInvoke) replyNull();
    return;
  }

  // ---- chrome ----
  if (channel == "chrome:bounds") {
    TabManager* t = needTabs();
    if (t && arg.isObject()) {
      const Json* w = arg.find("width");
      const Json* h = arg.find("height");
      double width = w ? w->numberOr(0) : 0;
      double height = h ? h->numberOr(0) : 0;
      if (std::isfinite(width) && std::isfinite(height)) {
        double x = arg.find("x") ? arg.find("x")->numberOr(0) : 0;
        double y = arg.find("y") ? arg.find("y")->numberOr(0) : 0;
        t->setBounds(static_cast<int>(clampNumber(x, -10000, 10000, 0)),
                     static_cast<int>(clampNumber(y, -10000, 10000, 0)),
                     static_cast<int>(clampNumber(width, 0, 10000, 0)),
                     static_cast<int>(clampNumber(height, 0, 10000, 0)));
      }
    }
    if (isInvoke) replyNull();
    return;
  }
  if (channel == "chrome:popup") {
    TabManager* t = needTabs();
    if (t) t->setPopupHidden(arg.isObject() && arg.find("open")
                                 ? arg.find("open")->boolOr(false)
                                 : false);
    return;
  }
  if (channel == "ctx:command") {
    TabManager* t = needTabs();
    if (!t) return;
    std::string cid = arg.isObject() && arg.find("id")
                          ? arg.find("id")->stringOr("")
                          : "";
    if (!contextCommandOk(cid)) return;
    // Find the tab that owns the pending context params (the right-clicked
    // tab may differ from the active tab).
    Tab* tab = t->active();
    if (tab && tab->lastCtx.isNull()) {
      t->each([&tab](Tab* candidate) {
        if (!tab->lastCtx.isNull()) return;
        if (candidate && !candidate->lastCtx.isNull()) tab = candidate;
      });
    }
    if (!tab || tab->lastCtx.isNull()) return;
    const Json* params = tab->lastCtx.find("params");
    Json p = params ? *params : Json::makeObject();
    tab->lastCtx = Json::makeNull();
    CommandCtx c;
    c.win = ctx.window ? ctx.window : t->window();
    c.tabs = t;
    c.tab = tab;
    runContextCommand(cid, p, c);
    return;
  }

  // ---- omni box ----
  if (channel == "omni:suggest") {
    std::string q = sanitizeText(arg.isObject() && arg.find("q")
                                     ? arg.find("q")->stringOr("")
                                     : "",
                                 200);
    Json out = Json::makeArray();
    if (q.empty()) {
      reply(std::move(out));
      return;
    }
    std::string lq = asciiLower(q);
    std::map<std::string, bool> seen;
    for (const auto& b : storage_.listBookmarks()) {
      if (b.url.empty()) continue;
      std::string name = asciiLower(b.name);
      std::string url = asciiLower(b.url);
      if (name.find(lq) == std::string::npos &&
          url.find(lq) == std::string::npos)
        continue;
      if (seen[b.url]) continue;
      seen[b.url] = true;
      Json o = Json::makeObject();
      o.obj.emplace_back("type", Json::makeString("bookmark"));
      o.obj.emplace_back("title", Json::makeString(b.name));
      o.obj.emplace_back("url", Json::makeString(b.url));
      out.arr.push_back(std::move(o));
    }
    HistoryQuery hq;
    hq.q = lq;
    hq.limit = 8;
    for (const auto& h : storage_.history(hq)) {
      if (seen[h.url] || out.arr.size() >= 8) continue;
      seen[h.url] = true;
      Json o = Json::makeObject();
      o.obj.emplace_back("type", Json::makeString("history"));
      o.obj.emplace_back("title", Json::makeString(h.title.empty() ? h.url : h.title));
      o.obj.emplace_back("url", Json::makeString(h.url));
      out.arr.push_back(std::move(o));
    }
    while (out.arr.size() > 8) out.arr.pop_back();
    reply(std::move(out));
    return;
  }

  // ---- bookmarks ----
  if (channel.rfind("bookmarks:", 0) == 0) {
    if (channel == "bookmarks:list") {
      Json arr = Json::makeArray();
      for (const auto& b : storage_.listBookmarks())
        arr.arr.push_back(bookmarkToJson(b));
      reply(std::move(arr));
      return;
    }
    if (channel == "bookmarks:add") {
      BookmarkIn in;
      if (!sanitizeBookmark(arg, in)) {
        replyNull();
        return;
      }
      BookmarkRow created = storage_.addBookmark(in.parent, in.name, in.url, -1);
      broadcast("bookmarks:changed", Json::makeNull());
      reply(bookmarkToJson(created));
      return;
    }
    if (channel == "bookmarks:update") {
      int64_t bid = 0;
      const Json* idv = arg.isObject() ? arg.find("id") : nullptr;
      if (!idv || !sanitizeId(*idv, bid)) {
        replyNull();
        return;
      }
      BookmarkIn patch;
      Json raw = Json::makeObject();
      if (arg.isObject()) {
        if (arg.find("name")) raw.obj.emplace_back("name", *arg.find("name"));
        if (arg.find("url")) raw.obj.emplace_back("url", *arg.find("url"));
        raw.obj.emplace_back("parent", Json::makeNumber(0));
      }
      if (!sanitizeBookmark(raw, patch)) {
        replyNull();
        return;
      }
      storage_.updateBookmark(bid, patch.name, patch.url);
      broadcast("bookmarks:changed", Json::makeNull());
      BookmarkRow out;
      if (storage_.getBookmark(bid, out)) reply(bookmarkToJson(out));
      else replyNull();
      return;
    }
    if (channel == "bookmarks:remove") {
      int64_t bid = 0;
      const Json* idv = arg.isObject() ? arg.find("id") : nullptr;
      if (idv && sanitizeId(*idv, bid)) {
        storage_.deleteBookmarkTree(bid);
        broadcast("bookmarks:changed", Json::makeNull());
      }
      if (isInvoke) replyNull();
      return;
    }
    if (channel == "bookmarks:toggle-quick") {
      int64_t bid = 0;
      const Json* idv = arg.isObject() ? arg.find("id") : nullptr;
      if (idv && sanitizeId(*idv, bid)) {
        bool quick = false;
        storage_.toggleBookmarkQuick(bid, &quick);
        broadcast("bookmarks:changed", Json::makeNull());
        Json o = Json::makeObject();
        o.obj.emplace_back("quick", Json::makeBool(quick));
        if (isInvoke) reply(std::move(o));
        return;
      }
      if (isInvoke) replyNull();
      return;
    }
    if (channel == "bookmarks:quick") {
      Json arr = Json::makeArray();
      int limit = 12;
      const Json* lv = arg.isObject() ? arg.find("limit") : nullptr;
      if (lv && lv->isNumber()) limit = std::max(1, std::min((int)lv->num, 32));
      int n = 0;
      for (const auto& b : storage_.listQuickBookmarks()) {
        if (n++ >= limit) break;
        arr.arr.push_back(bookmarkToJson(b));
      }
      reply(std::move(arr));
      return;
    }
    if (channel == "bookmarks:export") {
      HWND owner = ctx.window ? ctx.window->hwnd()
                              : (ctx.dialog ? ctx.dialog->hwnd() : nullptr);
      std::string path = dialogs::saveFileDialog(owner, "Export Favorites",
                                                 "web98-favorites.html.json",
                                                 {"json"});
      if (path.empty()) {
        Json o = Json::makeObject();
        o.obj.emplace_back("ok", Json::makeBool(false));
        reply(std::move(o));
        return;
      }
      Json root = Json::makeObject();
      Json arr = Json::makeArray();
      for (const auto& b : storage_.listBookmarks())
        arr.arr.push_back(bookmarkToJson(b));
      root.obj.emplace_back("bookmarks", std::move(arr));
      bool ok = writeFileAtomic(path, root.dump());
      Json o = Json::makeObject();
      o.obj.emplace_back("ok", Json::makeBool(ok));
      if (ok) o.obj.emplace_back("path", Json::makeString(path));
      else {
        o.obj.emplace_back("error", Json::makeString("write failed"));
      }
      reply(std::move(o));
      return;
    }
    if (channel == "bookmarks:import") {
      HWND owner = ctx.window ? ctx.window->hwnd()
                              : (ctx.dialog ? ctx.dialog->hwnd() : nullptr);
      std::string path = dialogs::openFileDialog(owner, "Import Favorites",
                                                 {"json"});
      Json o = Json::makeObject();
      if (path.empty()) {
        o.obj.emplace_back("ok", Json::makeBool(false));
        reply(std::move(o));
        return;
      }
      std::string text;
      Json root;
      if (!readFile(path, text) || !Json::parse(text, root)) {
        o.obj.emplace_back("ok", Json::makeBool(false));
        o.obj.emplace_back("error", Json::makeString("bad format"));
        reply(std::move(o));
        return;
      }
      const Json* list = root.find("bookmarks");
      std::vector<BookmarkIn> rows =
          sanitizeBookmarkList(list ? *list : Json::makeNull());
      bool hadList = list && list->isArray();
      if (rows.empty() && !(hadList && list->arr.empty())) {
        o.obj.emplace_back("ok", Json::makeBool(false));
        o.obj.emplace_back("error", Json::makeString("bad format"));
        reply(std::move(o));
        return;
      }
      for (const auto& b : rows)
        storage_.addBookmark(b.parent, b.name, b.url, -1);
      broadcast("bookmarks:changed", Json::makeNull());
      o.obj.emplace_back("ok", Json::makeBool(true));
      o.obj.emplace_back("count", Json::makeNumber(static_cast<double>(rows.size())));
      reply(std::move(o));
      return;
    }
  }

  // ---- history ----
  if (channel.rfind("history:", 0) == 0) {
    if (channel == "history:list") {
      HistoryQuery q;
      if (arg.isObject()) {
        const Json* qq = arg.find("q");
        if (qq && qq->isString()) q.q = qq->str.substr(0, 200);
        const Json* lim = arg.find("limit");
        if (lim && lim->isNumber()) {
          q.limit = static_cast<int>(clampNumber(lim->num, 1, 500, 200));
        }
      }
      Json arr = Json::makeArray();
      for (const auto& h : storage_.history(q)) arr.arr.push_back(historyToJson(h));
      reply(std::move(arr));
      return;
    }
    if (channel == "history:delete") {
      int64_t hid = 0;
      const Json* idv = arg.isObject() ? arg.find("id") : nullptr;
      if (idv && sanitizeId(*idv, hid)) {
        storage_.deleteHistory(hid);
        broadcast("history:changed", Json::makeNull());
      }
      if (isInvoke) replyNull();
      return;
    }
    if (channel == "history:clear") {
      storage_.clearHistory();
      broadcast("history:changed", Json::makeNull());
      if (isInvoke) replyNull();
      return;
    }
  }

  // ---- downloads ----
  if (channel.rfind("downloads:", 0) == 0) {
    auto idOf = [&](int64_t& out) {
      const Json* idv = arg.isObject() ? arg.find("id") : nullptr;
      return idv && sanitizeId(*idv, out);
    };
    if (channel == "downloads:list") {
      Json arr = Json::makeArray();
      for (const auto& d : downloads_.list()) arr.arr.push_back(downloadToJson(d));
      reply(std::move(arr));
      return;
    }
    int64_t did = 0;
    bool has = idOf(did);
    if (channel == "downloads:pause") {
      if (has) downloads_.Pause(static_cast<int>(did));
    } else if (channel == "downloads:resume") {
      if (has) downloads_.Resume(static_cast<int>(did));
    } else if (channel == "downloads:cancel") {
      if (has) downloads_.Cancel(static_cast<int>(did));
    } else if (channel == "downloads:remove") {
      if (has) downloads_.Remove(static_cast<int>(did));
    } else if (channel == "downloads:clearFinished") {
      downloads_.ClearFinished();
    } else if (channel == "downloads:open") {
      Json o = Json::makeObject();
      if (!has) {
        o.obj.emplace_back("ok", Json::makeBool(false));
        o.obj.emplace_back("error", Json::makeString("unknown download"));
        reply(std::move(o));
        return;
      }
      confirmDangerousDownload(static_cast<int>(did));
      o.obj.emplace_back("ok", Json::makeBool(true));
      reply(std::move(o));
      return;
    } else if (channel == "downloads:showFolder") {
      Json o = Json::makeObject();
      if (!has) {
        o.obj.emplace_back("ok", Json::makeBool(false));
        o.obj.emplace_back("error", Json::makeString("unknown download"));
        reply(std::move(o));
        return;
      }
      std::string p = downloads_.PathFor(static_cast<int>(did));
      bool ok = !p.empty() && dialogs::showInFolder(p);
      o.obj.emplace_back("ok", Json::makeBool(ok));
      reply(std::move(o));
      return;
    } else {
      if (isInvoke) replyNull();
      return;
    }
    // CEF pause/resume/cancel hooks fire their own listener notify.
    OnDownloadsChanged();
    if (isInvoke) replyNull();
    return;
  }

  // ---- settings ----
  if (channel == "settings:get") {
    reply(settings_.all());
    return;
  }
  if (channel == "settings:set") {
    reply(applySettingsPatch(arg));
    return;
  }
  if (channel == "settings:pickDownloadsDir") {
    if (!isChrome() && !isDialog()) {
      replyNull();
      return;
    }
    HWND owner = ctx.window ? ctx.window->hwnd()
                            : (ctx.dialog ? ctx.dialog->hwnd() : nullptr);
    std::string dir = dialogs::folderDialog(owner, "Choose Downloads Folder");
    if (dir.empty() || !isAbsolutePathNative(dir) ||
        dir.find('\0') != std::string::npos) {
      replyNull();
      return;
    }
    reply(Json::makeString(dir));
    return;
  }
  if (channel == "settings:clearData") {
    bool history = arg.isObject() && arg.find("history") &&
                   arg.find("history")->boolOr(false);
    bool cookies = arg.isObject() && arg.find("cookies") &&
                   arg.find("cookies")->boolOr(false);
    bool cache = arg.isObject() && arg.find("cache") &&
                 arg.find("cache")->boolOr(false);
    if (history) storage_.clearHistory();
    CefRefPtr<CefRequestContext> rc = CefRequestContext::GetGlobalContext();
    if (rc && cookies) {
      CefRefPtr<CefCookieManager> cm = rc->GetCookieManager(nullptr);
      if (cm) cm->DeleteCookies("", "", nullptr);
    }
    if (rc && cache) rc->ClearHttpCache(nullptr);
    broadcast("history:changed", Json::makeNull());
    Json o = Json::makeObject();
    o.obj.emplace_back("ok", Json::makeBool(true));
    reply(std::move(o));
    return;
  }
  if (channel == "settings:grants") {
    Json out;
    listGrants(out);
    reply(std::move(out));
    return;
  }
  if (channel == "settings:revoke") {
    if (arg.isObject()) {
      const Json* origin = arg.find("origin");
      const Json* perm = arg.find("permission");
      if (origin && perm && isGrantOrigin(origin->stringOr("")) &&
          isPermissionName(perm->stringOr(""))) {
        setGrant(origin->str, perm->str, Json::makeNull());
      }
    }
    Json out;
    listGrants(out);
    reply(std::move(out));
    return;
  }

  // ---- windows / app ----
  if (channel == "window:open") {
    std::string page = arg.isObject() && arg.find("page")
                           ? arg.find("page")->stringOr("")
                           : "";
    bool ok = openDialog(page, ctx.window ? ctx.window : primaryWindow());
    reply(ok ? Json::makeString(page) : Json::makeNull());
    return;
  }
  if (channel == "window:close") {
    if (ctx.dialog) ctx.dialog->requestClose();
    else if (ctx.window) ctx.window->requestClose();
    if (isInvoke) replyNull();
    return;
  }
  if (channel == "app:open-url") {
    if (!isChrome() && !isDialog()) {
      reply(Json::makeBool(false));
      return;
    }
    std::string url = arg.isObject() && arg.find("url")
                          ? arg.find("url")->stringOr("")
                          : "";
    if (!isLoadableUrl(url)) {
      reply(Json::makeBool(false));
      return;
    }
    MainWindow* w = primaryWindow();
    if (!w || !w->tabs()) {
      reply(Json::makeBool(false));
      return;
    }
    w->tabs()->load(w->tabs()->active(), url);
    w->focus();
    reply(Json::makeBool(true));
    return;
  }
  if (channel == "app:command") {
    MainWindow* w = ctx.window ? ctx.window
                              : (ctx.tabs ? ctx.tabs->window() : nullptr);
    if (!w) {
      reply(Json::makeBool(false));
      return;
    }
    std::string name = arg.isObject() && arg.find("name")
                           ? arg.find("name")->stringOr("")
                           : "";
    if (!commandNameOk(name)) {
      reply(Json::makeBool(false));
      return;
    }
    Json a = Json::makeNull();
    if (arg.isObject() && arg.find("arg")) {
      const Json& v = *arg.find("arg");
      if (v.isString()) a = Json::makeString(v.str.substr(0, 200));
      else if (v.isNumber() && std::isfinite(v.num)) a = v;
    }
    CommandCtx c;
    c.win = w;
    c.tabs = w->tabs();
    bool ok = runCommand(name, a, c);
    reply(Json::makeBool(ok));
    return;
  }
  if (channel == "app:view-source") {
    MainWindow* w = ctx.window ? ctx.window
                              : (ctx.tabs ? ctx.tabs->window() : nullptr);
    TabManager* t = w ? w->tabs() : nullptr;
    Tab* tab = t ? t->active() : nullptr;
    Json o = Json::makeObject();
    if (!tab || !tab->client || !tab->client->browser() ||
        !tab->client->browser()->GetMainFrame()) {
      o.obj.emplace_back("html", Json::makeString(""));
      o.obj.emplace_back("url", Json::makeString(tab ? tab->url : ""));
      reply(std::move(o));
      return;
    }
    std::string url = tab->url;
    tab->client->browser()->GetMainFrame()->GetSource(
        new StringSink([frame, id, url](const CefString& s) {
          Json out = Json::makeObject();
          out.obj.emplace_back("html", Json::makeString(s.ToString()));
          out.obj.emplace_back("url", Json::makeString(url));
          SendIpcReply(frame, id, true, out);
        }));
    return;
  }
  if (channel == "app:stats") {
    PROCESS_MEMORY_COUNTERS pmc{};
    Json o = Json::makeObject();
    double rss = 0, heap = 0;
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc))) {
      rss = static_cast<double>(pmc.WorkingSetSize) / 1048576.0;
      heap = static_cast<double>(pmc.PagefileUsage) / 1048576.0;
    }
    o.obj.emplace_back("rssMB", Json::makeNumber(std::round(rss * 10.0) / 10.0));
    o.obj.emplace_back("heapMB", Json::makeNumber(std::round(heap * 10.0) / 10.0));
    o.obj.emplace_back("uptimeS",
                       Json::makeNumber(
                           std::round(static_cast<double>(nowMs() - startupAtMs_) /
                                      100.0) /
                           10.0));
    reply(std::move(o));
    return;
  }

  // ---- certificate flow ----
  if (channel == "cert:proceed") {
    if (!isTab() || !ctx.tab || !ctx.tabs) return;
    Tab* tab = ctx.tab;
    std::string url = arg.isString() ? arg.str : "";
    if (!tab->error.isObject()) return;
    const Json* kind = tab->error.find("kind");
    if (!kind || kind->stringOr("") != "cert") return;
    if (tab->url != url) return;
    std::string current =
        tab->client && tab->client->browser() && tab->client->browser()->GetMainFrame()
            ? tab->client->browser()->GetMainFrame()->GetURL().ToString()
            : std::string();
    size_t cut = current.find_first_of("?#");
    if (cut != std::string::npos) current.resize(cut);
    if (current != errorPageUrl()) return;
    const int browserId =
        tab->client && tab->client->browser()
            ? tab->client->browser()->GetIdentifier()
            : -1;
    if (browserId < 0) return;
    certs_.grant(std::to_string(browserId) + "|" + url);
    tab->error = Json::makeNull();
    tab->errorPageShown = false;
    tab->expectedNav = url;
    if (tab->client && tab->client->browser() &&
        tab->client->browser()->GetMainFrame()) {
      tab->client->browser()->GetMainFrame()->LoadURL(url);
    }
    return;
  }

  // ---- home start page (web98home.action bridge) ----
  if (channel == "home:action") {
    if (!isTab() || !ctx.tab || !ctx.tabs) {
      reply(Json::makeBool(false));
      return;
    }
    std::string name = arg.isObject() && arg.find("name")
                           ? arg.find("name")->stringOr("")
                           : "";
    std::string value = arg.isObject() && arg.find("arg")
                            ? arg.find("arg")->stringOr("")
                            : "";
    if (name == "search") {
      AddressResult parsed = parseAddress(
          value, searchTemplateFor(settings_.all()), "http");
      if (parsed.type == AddressType::Empty ||
          !isLoadableUrl(parsed.url)) {
        reply(Json::makeBool(false));
        return;
      }
      ctx.tabs->load(ctx.tab, parsed.url);
      reply(Json::makeBool(true));
      return;
    }
    // favorites / history / settings / about open the matching dialog.
    if (name == "favorites" || name == "history" || name == "settings" ||
        name == "about") {
      MainWindow* w = ctx.window ? ctx.window : primaryWindow();
      reply(Json::makeBool(openDialog(name, w)));
      return;
    }
    reply(Json::makeBool(false));
    return;
  }

  if (isInvoke) replyError(id, frame, "Unknown IPC channel: " + channel);
}

/* --------------------------- downloads (CEF ids) ---------------------------- */

int AppCore::registerCefDownload(uint32_t cefId, const std::string& url,
                                 const std::string& suggestedName,
                                 std::string* outPath) {
  auto it = cefDownloadIds_.find(cefId);
  if (it != cefDownloadIds_.end()) {
    if (outPath) *outPath = downloads_.PathFor(it->second);
    return it->second;
  }
  int id = downloads_.RegisterNew(url, sanitizeFilename(suggestedName), outPath);
  if (id > 0) cefDownloadIds_[cefId] = id;
  return id;
}

void AppCore::updateCefDownload(CefRefPtr<CefDownloadItem> item,
                                CefRefPtr<CefDownloadItemCallback> callback) {
  if (!item || !item->IsValid()) return;
  auto it = cefDownloadIds_.find(item->GetId());
  if (it == cefDownloadIds_.end()) {
    std::string name = item->GetSuggestedFileName().ToString();
    if (name.empty()) name = pathBaseName(item->GetURL().ToString());
    std::string path;
    registerCefDownload(item->GetId(), item->GetURL().ToString(), name, &path);
    it = cefDownloadIds_.find(item->GetId());
    if (it == cefDownloadIds_.end()) return;
  }
  int id = it->second;

  if (item->IsComplete()) {
    downloads_.Done(id, "completed");
    return;
  }
  if (item->IsCanceled()) {
    downloads_.Done(id, "cancelled");
    return;
  }
  if (item->IsInterrupted()) {
    downloads_.Done(id, "interrupted");
    return;
  }
  if (item->IsInProgress() || item->IsPaused()) {
    downloads_.Progress(id, item->GetReceivedBytes(), item->GetTotalBytes(),
                        item->IsPaused());
  }
  if (item->IsInProgress() && !item->IsPaused()) {
    CefRefPtr<CefDownloadItemCallback> cb = callback;
    downloads_.SetCancelHook(id, [cb]() { cb->Cancel(); });
    downloads_.SetPauseHook(id, [cb]() { cb->Pause(); });
    downloads_.SetResumeHook(id, [cb]() { cb->Resume(); });
  }
}

void AppCore::confirmDangerousDownload(int id) {
  std::string p = downloads_.PathFor(id);
  Json o = Json::makeObject();
  if (p.empty() || !fileExists(p)) {
    o.obj.emplace_back("ok", Json::makeBool(false));
    o.obj.emplace_back("error", Json::makeString("File no longer exists"));
    return;
  }
  std::string filename = pathBaseName(p);
  if (Downloads::IsDangerousExtension(filename)) {
    std::string ext;
    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos) ext = asciiLower(filename.substr(dot));
    std::string text = "This file type can harm your computer.\n\n" +
                       filename + " is a " + ext +
                       " file. Only open it if you trust the source.";
    int r = dialogs::messageBox(nullptr, text, "Web98",
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (r != IDYES) return;
  }
  dialogs::shellOpenPath(p);
}

}  // namespace web98
