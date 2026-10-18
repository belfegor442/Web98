#include "cef_engine.h"

#include <algorithm>
#include <cstdlib>
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

// Sleep threshold in ms. WEB98_SLEEP_DELAY_MS (test hook) overrides the
// user setting so verification scripts can exercise the full cycle fast.
int64_t sleepDelayMs() {
  char env[32];
  DWORD n = ::GetEnvironmentVariableA("WEB98_SLEEP_DELAY_MS", env, sizeof(env));
  if (n > 0 && n < sizeof(env)) {
    long v = std::atol(env);
    if (v >= 0) return v;
  }
  double sec =
      AppCore::get().settingsModel().getNumber("sleepTabsDelay", 300);
  if (sec < 30) sec = 30;
  if (sec > 43200) sec = 43200;
  return static_cast<int64_t>(sec * 1000);
}

}  // namespace

// Delivers CDP method results (the pre-discard scroll/audio probe) back to
// the TabManager. Guarded by the manager's alive token: the observer can
// outlive a browser teardown that races window destruction.
class SleepProbeObserver : public CefDevToolsMessageObserver {
 public:
  SleepProbeObserver(TabManager* mgr, int tabId, std::weak_ptr<int> alive)
      : mgr_(mgr), tabId_(tabId), alive_(std::move(alive)) {}

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id,
                              bool success, const void* result,
                              size_t result_size) override {
    (void)browser;
    if (alive_.expired() || !mgr_) return;
    mgr_->OnDevToolsResult(
        tabId_, message_id, success,
        result ? std::string(static_cast<const char*>(result), result_size)
               : std::string());
  }

 private:
  TabManager* mgr_;
  int tabId_;
  std::weak_ptr<int> alive_;
  IMPLEMENT_REFCOUNTING(SleepProbeObserver);
  DISALLOW_COPY_AND_ASSIGN(SleepProbeObserver);
};

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
    o.obj.emplace_back("pinned", Json::makeBool(t->pinned));
    o.obj.emplace_back("slept", Json::makeBool(t->slept));
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

int TabManager::indexOf(int id) const {
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i]->id == id) return static_cast<int>(i);
  return -1;
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
  tab->lastActiveMs = nowMs();
  tabs_.push_back(std::move(owned));
  scheduleSweep(2000);  // memory saver heartbeat (self-perpetuating)

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
    // A slept (discarded) tab reloads its saved URL on wake.
    std::string queued = tab->pendingUrl;
    tab->pendingUrl.clear();
    if (queued.empty() && tab->slept) queued = tab->url;
    ensureBrowser(tab, queued);
    if (tab->slept) {
      tab->slept = false;
      tab->loading = true;  // spinner while the renderer reboots
      tab->progress = 0.05;
      tab->progressBucket = -1;
      tab->error = Json::makeNull();
      emitTabs();
    }
  }
  tab->lastActiveMs = nowMs();  // memory saver: foreground clock starts now
  if (activeId_ == id) {
    syncVisibility();
    HWND h = hwndOf(tab);
    if (h) ::SetFocus(h);
    navState(tab);
    return;
  }
  // The outgoing tab just went background: its sleep timer starts now
  // (inactivity is measured from the switch-away, not the switch-to).
  if (Tab* prev = get(activeId_)) prev->lastActiveMs = nowMs();
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

  // Remember the closed tab for Ctrl+Shift+T (skip empty new tabs).
  if (!tab->url.empty()) {
    closedTabs_.push_back({tab->url, tab->title});
    if (closedTabs_.size() > 10) closedTabs_.erase(closedTabs_.begin());
  }

  if (!destroyed && tab->client) {
    CefRefPtr<CefBrowser> b = tab->client->browser();
    tab->client->Detach();
    if (b) b->GetHost()->CloseBrowser(false);
    tab->client = nullptr;
  }

  if (activeId_ == id) {
    activeId_ = 0;
    if (!tabs_.empty()) {
      Tab* next = tabs_[std::min<size_t>(idx, tabs_.size() - 1)].get();
      if (next) setActive(next->id);
    } else if (win_) {
      win_->requestClose();
    }
  }
  // If the closed tab had a background load in flight, advance the queue.
  auto inflight = std::find(flushInflight_.begin(), flushInflight_.end(), id);
  if (inflight != flushInflight_.end()) {
    flushInflight_.erase(inflight);
    scheduleFlush(100);
  }
  emitTabs();
}

Tab* TabManager::duplicate(int id) {
  Tab* src = get(id);
  if (!src) return nullptr;
  Tab* copy = create(src->url, true);
  return copy;
}

Tab* TabManager::reopenClosed() {
  if (closedTabs_.empty()) return nullptr;
  ClosedTab last = closedTabs_.back();
  closedTabs_.pop_back();
  return create(last.url, true);
}

void TabManager::move(int id, int beforeId) {
  int from = indexOf(id);
  int to = indexOf(beforeId);
  if (from < 0 || to < 0 || from == to) return;
  std::unique_ptr<Tab> item = std::move(tabs_[from]);
  tabs_.erase(tabs_.begin() + from);
  if (from < to) --to;
  tabs_.insert(tabs_.begin() + to, std::move(item));
  emitTabs();
}

void TabManager::togglePin(int id) {
  int idx = indexOf(id);
  if (idx < 0) return;
  std::unique_ptr<Tab> item = std::move(tabs_[idx]);
  tabs_.erase(tabs_.begin() + idx);
  item->pinned = !item->pinned;
  // Pinned tabs live at the front of the strip, preserving their relative
  // order among pinned tabs; unpinned tabs go back after the pinned block.
  size_t insert = 0;
  if (!item->pinned) {
    while (insert < tabs_.size() && tabs_[insert]->pinned) ++insert;
  }
  tabs_.insert(tabs_.begin() + insert, std::move(item));
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
  // Memory saver: watch for scroll/audio probe results (no DevTools window
  // or remote-debugging session needed; CEF 154 delivers them to observers).
  if (CefRefPtr<CefBrowserHost> host = browser->GetHost()) {
    std::weak_ptr<int> weak = alive_;
    tab->devtoolsReg =
        host->AddDevToolsMessageObserver(new SleepProbeObserver(this, tab->id, weak));
  }
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
  tab->savedScroll = 0;  // fresh navigation supersedes a wake restore
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
    // Keep tab->error while the error page is loading so the "Continue
    // anyway" button can read it. Clear it only for normal navigations.
    if (!tab->errorPageShown) tab->error = Json::makeNull();
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
    // If this is NOT the error page (load failed / user navigated), clear
    // the flag so future errors are not swallowed by showError's guard.
    if (url.empty() || url.compare(0, errorPageUrl().size(),
                                   errorPageUrl()) != 0) {
      tab->errorPageShown = false;
      tab->error = Json::makeNull();
    } else {
      return;
    }
  }
  tab->url = url;
  tab->error = Json::makeNull();
  tab->crashedOnce = false;
  // Apply per-site zoom memory.
  std::string origin = originOf(url);
  if (!origin.empty() && origin != "file://") {
    double sz = AppCore::get().siteZoom(origin);
    if (sz > 0 && std::abs(sz - tab->zoom) > 0.001) {
      tab->zoom = sz;
      if (tab->client && tab->client->browser() && tab->client->browser()->GetHost())
        tab->client->browser()->GetHost()->SetZoomLevel(std::log(sz) / std::log(1.2));
    }
  }
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
  // Memory saver: re-apply the scroll position captured before the tab was
  // discarded (Chrome restores scroll the same way after a discard reload).
  if (tab && tab->savedScroll > 0 && tab->client && tab->client->browser()) {
    double y = tab->savedScroll;
    tab->savedScroll = 0;
    CefRefPtr<CefBrowserHost> host = tab->client->browser()->GetHost();
    if (host) {
      CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
      params->SetString("expression",
                        "window.scrollTo(0," + std::to_string(static_cast<int>(y)) + ")");
      host->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
    }
  }
  // A background-load turn completed: refill the parallel restore pipeline.
  if (tab) {
    auto it = std::find(flushInflight_.begin(), flushInflight_.end(), tab->id);
    if (it != flushInflight_.end()) {
      flushInflight_.erase(it);
      scheduleFlush(0);
    }
  }
}

void TabManager::OnLoadError(Tab* tab, int code, const std::string& desc,
                             const std::string& url) {
  // Failed background loads must also advance the flush queue.
  if (tab) {
    auto it = std::find(flushInflight_.begin(), flushInflight_.end(), tab->id);
    if (it != flushInflight_.end()) {
      flushInflight_.erase(it);
      scheduleFlush(200);
    }
  }
  // If the error page itself failed to load, clear the guard so the next
  // error is not swallowed by showError's errorPageShown check.
  if (tab && tab->errorPageShown) {
    const std::string ep = errorPageUrl();
    if (url.empty() || url.compare(0, ep.size(), ep) != 0)
      tab->errorPageShown = false;
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
  // Drop jobs whose tab went away or whose URL was superseded.
  std::vector<Job> keep;
  for (const auto& job : pendingLoads_) {
    Tab* t = get(job.tabId);
    if (t && t->pendingUrl == job.url) keep.push_back(job);
  }
  pendingLoads_.swap(keep);

  // Parallel restore: start background loads up to the concurrency cap so a
  // session with many tabs does not wait for one full page round-trip each.
  constexpr size_t kMaxConcurrent = 3;
  while (flushInflight_.size() < kMaxConcurrent && !pendingLoads_.empty()) {
    Job job = pendingLoads_.front();
    pendingLoads_.erase(pendingLoads_.begin());
    Tab* tab = get(job.tabId);
    if (!tab) continue;
    tab->pendingUrl.clear();
    if (!ensureBrowser(tab, job.url)) continue;
    flushInflight_.push_back(tab->id);
    int tabId = tab->id;
    std::weak_ptr<int> weak = alive_;
    PostUiDelayed(
        [this, weak, tabId]() {
          if (weak.expired()) return;
          auto it =
              std::find(flushInflight_.begin(), flushInflight_.end(), tabId);
          if (it == flushInflight_.end()) return;  // completed already
          flushInflight_.erase(it);
          scheduleFlush(100);
        },
        15000);
  }
}

/* ------------------------- memory saver (sleep) --------------------------- */

void TabManager::scheduleSweep(int delayMs) {
  if (sweepScheduled_) return;
  sweepScheduled_ = true;
  std::weak_ptr<int> weak = alive_;
  PostUiDelayed(
      [this, weak]() {
        if (weak.expired()) return;
        sweepScheduled_ = false;
        sweep();
      },
      delayMs);
}

void TabManager::sweep() {
  // Self-perpetuating heartbeat while the window lives (alive_ guard above).
  if (win_) {
    bool enabled = AppCore::get().settingsModel().getBool("sleepTabs", true);
    if (enabled) {
      const int64_t delay = sleepDelayMs();
      const int64_t now = nowMs();
      for (auto& sp : tabs_) {
        Tab* t = sp.get();
        if (!t || t->id == activeId_ || t->pinned || t->slept) continue;
        if (!t->client || t->loading || t->probeInFlight) continue;
        if (t->lastActiveMs == 0) t->lastActiveMs = now;
        if (now - t->lastActiveMs < delay) continue;
        // Idle threshold reached: ask the renderer for scroll + audio state,
        // then discard (or re-check soon if media is still playing).
        startSleepProbe(t);
      }
    }
    scheduleSweep(2000);
  }
}

bool TabManager::startSleepProbe(Tab* t) {
  if (!t || !t->client) return false;
  CefRefPtr<CefBrowser> b = t->client->browser();
  if (!b || !b->GetHost()) return false;
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString(
      "expression",
      "(()=>{try{return JSON.stringify({y:window.scrollY|0,"
      "a:Array.prototype.some.call(document.querySelectorAll('video,audio'),"
      "function(m){return !m.paused && !m.ended;})})}"
      "catch(e){return '{\"y\":0,\"a\":false}'}})()");
  params->SetBool("returnByValue", true);
  int id = b->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", params);
  if (!id) return false;
  t->probeInFlight = true;
  t->probeMsgId = id;
  return true;
}

void TabManager::OnDevToolsResult(int tabId, int messageId, bool success,
                                  const std::string& json) {
  Tab* t = get(tabId);
  if (!t || !t->probeInFlight || t->probeMsgId != messageId) return;
  t->probeInFlight = false;
  t->probeMsgId = 0;
  // The tab was activated/closed/reloaded while the probe was in flight.
  if (!t->client || t->slept || t->id == activeId_) return;
  bool audio = false;
  double y = 0;
  if (success) {
    // {"result":{"type":"string","value":"{\"y\":123,\"a\":false}"}}
    Json outer;
    if (Json::parse(json, outer)) {
      if (const Json* r = outer.find("result")) {
        if (const Json* v = r->find("value")) {
          Json inner;
          if (v->isString() && Json::parse(v->str, inner)) {
            if (const Json* iy = inner.find("y")) y = iy->numberOr(0);
            if (const Json* ia = inner.find("a")) audio = ia->boolOr(false);
          }
        }
      }
    }
  }
  if (audio) {
    // Media playing: never kill the sound. Re-check in 30s.
    t->lastActiveMs = nowMs() + 30000;
    return;
  }
  t->savedScroll = y;
  sleepTab(t);
}

void TabManager::sleepTab(Tab* t) {
  if (!t || !t->client || t->slept) return;
  if (t->id == activeId_ || t->loading) return;
  CefRefPtr<CefBrowser> b = t->client->browser();
  // Discard: Detach stops lifecycle callbacks (the strip entry with URL,
  // title and favicon survives; setActive reloads it on wake). Force close
  // skips beforeunload like Chrome's Memory Saver discard.
  t->client->Detach();
  if (b && b->GetHost()) b->GetHost()->CloseBrowser(true);
  t->client = nullptr;
  t->devtoolsReg = nullptr;
  t->probeInFlight = false;
  t->probeMsgId = 0;
  t->loading = false;
  t->progress = 1;
  t->slept = true;
  emitTabs();
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
