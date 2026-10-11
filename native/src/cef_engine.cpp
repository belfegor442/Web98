#include "cef_engine.h"

#include <algorithm>
#include <regex>

#include "app_core.h"
#include "base.h"
#include "cef_util.h"
#include "dialogs.h"
#include "safeurl.h"
#include "security.h"
#include "window.h"

namespace web98 {
namespace {

// chrome:bounds reports CSS pixels (getBoundingClientRect); Win32 child
// placement needs physical pixels. Scale by the window DPI.
int cssToPhys(HWND hwnd, int v) {
  UINT dpi = ::GetDpiForWindow(hwnd);
  return dpi ? ::MulDiv(v, dpi, 96) : v;
}

bool isHttpUrl(const std::string& url) {
  return startsWith(url, "http:") || startsWith(url, "https:");
}

bool canGoBack(Tab* t) {
  if (!t || !t->client) return false;
  CefRefPtr<CefBrowser> b = t->client->browser();
  return b && b->CanGoBack();
}

bool canGoForward(Tab* t) {
  if (!t || !t->client) return false;
  CefRefPtr<CefBrowser> b = t->client->browser();
  return b && b->CanGoForward();
}

HWND hwndOf(Tab* t) {
  if (!t || !t->client) return nullptr;
  CefRefPtr<CefBrowser> b = t->client->browser();
  if (!b) return nullptr;
  CefRefPtr<CefBrowserHost> host = b->GetHost();
  if (!host) return nullptr;
  return reinterpret_cast<HWND>(host->GetWindowHandle());
}

}  // namespace

TabManager::TabManager(MainWindow* win, EmitFn emit)
    : win_(win), emit_(std::move(emit)) {}

TabManager::~TabManager() {
  for (auto& t : tabs_) {
    if (t->client) {
      t->client->Detach();
      CefRefPtr<CefBrowser> b = t->client->browser();
      if (b) b->GetHost()->CloseBrowser(true);
      t->client = nullptr;
    }
  }
  tabs_.clear();
}

std::string TabManager::zoneFor(const std::string& url) {
  static const std::regex re(
      "^file:|^http://(localhost|127\\.)", std::regex_constants::icase);
  static const std::regex reWeb("^https?://", std::regex_constants::icase);
  if (std::regex_search(url, re)) return "Local intranet";
  if (std::regex_search(url, reWeb)) return "Internet zone";
  return std::string();
}

/* ------------------------------- snapshot -------------------------------- */

Json TabManager::list() const {
  Json out = Json::makeArray();
  for (const auto& t : tabs_) {
    Json o = Json::makeObject();
    o.obj.emplace_back("id", Json::makeNumber(t->id));
    o.obj.emplace_back("url", Json::makeString(t->url));
    o.obj.emplace_back("title", Json::makeString(t->title));
    o.obj.emplace_back("favicon", Json::makeString(t->favicon));
    o.obj.emplace_back("loading", Json::makeBool(t->loading));
    o.obj.emplace_back("progress", Json::makeNumber(t->progress));
    o.obj.emplace_back("active", Json::makeBool(t->id == activeId_));
    out.arr.push_back(std::move(o));
  }
  return out;
}

Tab* TabManager::get(int id) {
  for (auto& t : tabs_)
    if (t->id == id) return t.get();
  return nullptr;
}

Tab* TabManager::active() { return get(activeId_); }

Tab* TabManager::findByBrowser(int browserId) {
  for (auto& t : tabs_) {
    if (!t->client) continue;
    CefRefPtr<CefBrowser> b = t->client->browser();
    if (b && b->GetIdentifier() == browserId) return t.get();
  }
  return nullptr;
}

Tab* TabManager::findByHwnd(HWND h) {
  if (!h) return nullptr;
  for (auto& t : tabs_) {
    if (hwndOf(t.get()) == h) return t.get();
  }
  return nullptr;
}

void TabManager::each(const std::function<void(Tab*)>& fn) {
  for (auto& t : tabs_) fn(t.get());
}

/* ------------------------------- creation -------------------------------- */

bool TabManager::ensureBrowser(Tab* tab, const std::string& initialUrl) {
  if (tab->client) return true;
  BrowserCtx ctx;
  ctx.role = BrowserCtx::kTab;
  ctx.window = win_;
  ctx.tabs = this;
  ctx.tab = tab;
  std::string url = initialUrl.empty() ? "about:blank" : initialUrl;
  tab->client = new Web98Client(ctx, url);

  CefWindowInfo wi;
  HWND owner = win_->hwnd();
  wi.SetAsChild(owner,
                CefRect(cssToPhys(owner, boundsX_), cssToPhys(owner, boundsY_),
                        cssToPhys(owner, boundsW_), cssToPhys(owner, boundsH_)));
  wi.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  if (!CefBrowserHost::CreateBrowser(wi, tab->client, url, settings, nullptr,
                                     nullptr)) {
    tab->client = nullptr;
    return false;
  }
  return true;
}

Tab* TabManager::create(const std::string& url, bool activate) {
  auto owned = std::make_unique<Tab>();
  Tab* tab = owned.get();
  tab->id = nextId_++;
  tab->zoom = AppCore::get().settingsModel().getNumber("zoom", 1);
  tabs_.push_back(std::move(owned));

  if (!url.empty()) {
    if (isSafeExternalUrl(url)) {
      ensureBrowser(tab, std::string());
      dialogs::openExternalUrl(url);
    } else if (isLoadableUrl(url)) {
      ensureBrowser(tab, url);
    }
  }
  if (activate) setActive(tab->id);
  emitTabs();
  return tab;
}

Tab* TabManager::createBackground(const std::string& url) {
  Tab* tab = create(std::string(), false);
  if (!url.empty() && isLoadableUrl(url)) {
    tab->url = url;
    tab->pendingUrl = url;
    pendingLoads_.push_back(Job{tab->id, url});
    scheduleFlush(400);
    emitTabs();
  }
  return tab;
}

void TabManager::setActive(int id) {
  Tab* tab = get(id);
  if (!tab) return;
  if (!tab->client) {
    // Queued background tab (or a brand-new one): materialize it now.
    std::string queued = tab->pendingUrl;
    tab->pendingUrl.clear();
    ensureBrowser(tab, queued);
  }
  if (activeId_ == id) {
    syncVisibility();
    HWND h = hwndOf(tab);
    if (h) ::SetFocus(h);
    navState(tab);
    return;
  }
  activeId_ = id;
  syncVisibility();
  HWND h = hwndOf(tab);
  if (h) ::SetFocus(h);
  emitTabs();
  navState(tab);
}

void TabManager::close(int id, bool destroyed) {
  int idx = -1;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i]->id == id) {
      idx = static_cast<int>(i);
      break;
    }
  }
  if (idx == -1) return;
  std::unique_ptr<Tab> owned = std::move(tabs_[idx]);
  tabs_.erase(tabs_.begin() + idx);
  Tab* tab = owned.get();

  if (!destroyed && tab->client) {
    CefRefPtr<CefBrowser> b = tab->client->browser();
    tab->client->Detach();
    if (b) b->GetHost()->CloseBrowser(true);
    tab->client = nullptr;
  }

  if (activeId_ == id) {
    activeId_ = 0;
    if (!tabs_.empty()) {
      Tab* next = tabs_[std::min<size_t>(idx, tabs_.size() - 1)].get();
      if (next) setActive(next->id);
    }
  }
  // If the closed tab was the in-flight flush job, advance the queue.
  if (flushJobId_ == id) {
    flushJobId_ = 0;
    ++flushToken_;
    scheduleFlush(100);
  }
  emitTabs();
}

void TabManager::OnTabCreated(Tab* tab, CefRefPtr<CefBrowser> browser) {
  // The tab may have been closed while CreateBrowser was in flight;
  // close the orphaned browser immediately.
  if (!tab || !tab->client || get(tab->id) != tab) {
    if (browser && browser->GetHost()) browser->GetHost()->CloseBrowser(true);
    return;
  }
  tab->client->SetBrowser(browser);
  applyBounds(tab);
  syncVisibility();
  HWND h = hwndOf(tab);
  if (h && win_ && win_->hwnd()) subclassChild(h, win_->hwnd());
  if (tab->id == activeId_ && h) ::SetFocus(h);
}

void TabManager::OnTabClosed(Tab* tab) {
  if (!tab) return;
  close(tab->id, true);
}

/* -------------------------------- loading -------------------------------- */

bool TabManager::load(Tab* tab, const std::string& url) {
  if (!tab || !tab->client || !tab->client->browser()) return false;
  tab->pendingUrl.clear();
  if (isSafeExternalUrl(url)) {
    dialogs::openExternalUrl(url);
    return true;
  }
  if (!isLoadableUrl(url)) return false;
  ++tab->navSeq;
  CefRefPtr<CefBrowser> b = tab->client->browser();
  if (!b || !b->GetMainFrame()) return false;
  tab->expectedNav = url;
  b->GetMainFrame()->LoadURL(url);
  return true;
}

void TabManager::showError(Tab* tab, int code, const std::string& desc,
                           const std::string& url) {
  if (!tab || tab->errorPageShown) return;
  std::string kind = classifyLoadError(code, desc);
  Json err = Json::makeObject();
  err.obj.emplace_back("code", Json::makeNumber(code));
  err.obj.emplace_back("desc", Json::makeString(desc));
  err.obj.emplace_back("url", Json::makeString(url));
  err.obj.emplace_back("kind", Json::makeString(kind));
  tab->error = std::move(err);
  tab->errorPageShown = true;
  tab->suppressCommit = true;
  ++tab->navSeq;
  if (!url.empty()) tab->url = url;

  std::string page = errorPageUrl();
  page += "?kind=" + urlEncodeQuery(kind) +
          "&code=" + urlEncodeQuery(std::to_string(code)) +
          "&desc=" + urlEncodeQuery(desc) + "&url=" + urlEncodeQuery(url);

  if (tab->client && tab->client->browser()) {
    CefRefPtr<CefBrowser> b = tab->client->browser();
    if (b && b->GetMainFrame()) {
      tab->expectedNav = page;
      b->GetMainFrame()->LoadURL(page);
    }
  }
  emitTabs();
  navState(tab);
}

void TabManager::commitHistory(Tab* tab) {
  if (!tab) return;
  if (isHttpUrl(tab->url)) {
    AppCore::get().storage().addHistory(tab->url, tab->title, nowMs());
  }
}

void TabManager::OnLoadingStateChange(Tab* tab, bool isLoading, bool,
                                      bool) {
  if (!tab) return;
  tab->loading = isLoading;
  if (isLoading) {
    tab->progress = 0.05;
    tab->progressBucket = -1;
    tab->error = Json::makeNull();
    emitTabs();
    navState(tab);
    return;
  }
  tab->progress = 1;
  emitTabs();
  navState(tab);
  if (!tab->suppressCommit && isHttpUrl(tab->url)) commitHistory(tab);
  tab->suppressCommit = false;
}

void TabManager::OnProgress(Tab* tab, double progress) {
  if (!tab) return;
  tab->progress = progress;
  int bucket = static_cast<int>(progress * 20 + 0.5);
  if (bucket == tab->progressBucket) return;
  tab->progressBucket = bucket;
  navState(tab);
}

void TabManager::OnAddressChange(Tab* tab, const std::string& url) {
  if (!tab) return;
  if (tab->errorPageShown) {
    // Internal error page load: keep the failed URL visible in the UI.
    tab->errorPageShown = false;
    return;
  }
  tab->url = url;
  tab->error = Json::makeNull();
  tab->crashedOnce = false;
  emitTabs();
  navState(tab);
}

void TabManager::OnTitleChange(Tab* tab, const std::string& title) {
  if (!tab) return;
  tab->title = title;
  emitTabs();
  navState(tab);
}

void TabManager::OnFaviconChange(Tab* tab, const std::string& iconUrl) {
  if (!tab) return;
  tab->favicon = iconUrl;
  emitTabs();
}

void TabManager::OnLoadEnd(Tab* tab) {
  // A background-load turn completed: schedule the next one (tabs.js uses
  // did-finish-load/did-fail-load plus a 15s watchdog).
  if (tab && flushJobId_ != 0 && tab->id == flushJobId_) {
    flushJobId_ = 0;
    ++flushToken_;  // invalidate the watchdog for this job
    scheduleFlush(200);
  }
}

void TabManager::OnLoadError(Tab* tab, int code, const std::string& desc,
                             const std::string& url) {
  // Failed background loads must also advance the flush queue.
  if (tab && flushJobId_ != 0 && tab->id == flushJobId_) {
    flushJobId_ = 0;
    ++flushToken_;
    scheduleFlush(200);
  }
  showError(tab, code, desc, url);
}

/* ------------------------------ bounds/zones ----------------------------- */

void TabManager::setBounds(int x, int y, int w, int h) {
  boundsX_ = x;
  boundsY_ = y;
  boundsW_ = w;
  boundsH_ = h;
  for (auto& t : tabs_) applyBounds(t.get());
}

void TabManager::applyBounds(Tab* tab) {
  HWND h = hwndOf(tab);
  if (!h) return;
  HWND owner = win_->hwnd();
  int px = cssToPhys(owner, boundsX_);
  int py = cssToPhys(owner, boundsY_);
  int pw = cssToPhys(owner, boundsW_);
  int ph = cssToPhys(owner, boundsH_);
  ::MoveWindow(h, px, py, pw, ph, TRUE);
}

void TabManager::setPopupHidden(bool hidden) {
  hiddenForPopup_ = hidden;
  syncVisibility();
  if (!hidden) {
    Tab* t = active();
    if (t) {
      HWND h = hwndOf(t);
      if (h) ::SetFocus(h);
    }
  }
}

void TabManager::syncVisibility() {
  for (auto& t : tabs_) {
    HWND h = hwndOf(t.get());
    if (!h) continue;
    bool show = (t->id == activeId_) && !hiddenForPopup_;
    ::ShowWindow(h, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (show) ::SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

void TabManager::setZoom(double factor) { setZoom(active(), factor); }

void TabManager::setZoom(Tab* t, double factor) {
  if (!t) return;
  t->zoom = factor;
  if (t->client && t->client->browser()) {
    CefRefPtr<CefBrowserHost> host = t->client->browser()->GetHost();
    if (host) host->SetZoomLevel(std::log(factor) / std::log(1.2));
  }
  navState(t);
}

/* ------------------------------ background loads ------------------------- */

void TabManager::scheduleFlush(int delayMs) {
  if (flushScheduled_) return;
  flushScheduled_ = true;
  std::weak_ptr<int> weak = alive_;
  PostUiDelayed(
      [this, weak]() {
        if (weak.expired()) return;
        flushScheduled_ = false;
        flushPending();
      },
      delayMs);
}

void TabManager::flushPending() {
  // A background load is already in flight; don't start another.
  if (flushJobId_ != 0) return;
  // Drop jobs whose tab went away or whose URL was superseded.
  std::vector<Job> keep;
  for (const auto& job : pendingLoads_) {
    Tab* t = get(job.tabId);
    if (t && t->pendingUrl == job.url) keep.push_back(job);
  }
  pendingLoads_.swap(keep);
  if (pendingLoads_.empty()) return;

  Job job = pendingLoads_.front();
  pendingLoads_.erase(pendingLoads_.begin());
  Tab* tab = get(job.tabId);
  if (!tab) {
    scheduleFlush(100);
    return;
  }
  tab->pendingUrl.clear();
  ensureBrowser(tab, job.url);

  flushJobId_ = tab->id;
  int token = ++flushToken_;
  std::weak_ptr<int> weak = alive_;
  PostUiDelayed(
      [this, weak, token]() {
        if (weak.expired()) return;
        if (token != flushToken_) return;  // completed already
        flushJobId_ = 0;
        scheduleFlush(100);
      },
      15000);
}

/* -------------------------------- events --------------------------------- */

void TabManager::emitTabs() {
  if (tabsEmitScheduled_) return;
  tabsEmitScheduled_ = true;
  std::weak_ptr<int> weak = alive_;
  PostUi([this, weak]() {
    if (weak.expired()) return;
    tabsEmitScheduled_ = false;
    Json snap = list();
    std::string json = snap.dump();
    if (json == lastTabsJson_) return;
    lastTabsJson_ = json;
    emit_("tabs", snap);
  });
}

void TabManager::navState() { navState(active()); }

void TabManager::navState(Tab* tab) {
  if (!tab || tab->id != activeId_) return;
  pendingNavId_ = tab->id;
  if (navEmitScheduled_) return;
  navEmitScheduled_ = true;
  std::weak_ptr<int> weak = alive_;
  PostUi([this, weak]() {
    if (weak.expired()) return;
    navEmitScheduled_ = false;
    Tab* t = get(pendingNavId_);
    if (!t || t->id != activeId_) return;
    Json payload = Json::makeObject();
    payload.obj.emplace_back("tabId", Json::makeNumber(t->id));
    payload.obj.emplace_back("url", Json::makeString(t->url));
    payload.obj.emplace_back("title", Json::makeString(t->title));
    payload.obj.emplace_back("loading", Json::makeBool(t->loading));
    payload.obj.emplace_back("progress", Json::makeNumber(t->progress));
    payload.obj.emplace_back("canBack", Json::makeBool(canGoBack(t)));
    payload.obj.emplace_back("canForward", Json::makeBool(canGoForward(t)));
    payload.obj.emplace_back("zoom", Json::makeNumber(t->zoom));
    payload.obj.emplace_back("zone", Json::makeString(zoneFor(t->url)));
    payload.obj.emplace_back("error", t->error);
    std::string json = payload.dump();
    if (json == lastNavJson_) return;
    lastNavJson_ = json;
    emit_("nav", payload);
  });
}

}  // namespace web98
