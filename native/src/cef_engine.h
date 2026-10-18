#pragma once
// Tab records + per-window tab management. Direct port of src/main/tabs.js:
// navigation lifecycle, error pages, deferred background loads, coalesced
// 'tabs'/'nav' events and the chrome-driven content bounds.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cef_client.h"
#include "include/cef_registration.h"
#include "json.h"

namespace web98 {

class MainWindow;

struct Tab {
  int id = 0;
  // Browser side (created lazily for queued background tabs).
  CefRefPtr<Web98Client> client;
  std::string url;
  std::string title = "New Tab";
  std::string favicon;
  bool loading = false;
  double progress = 0.0;
  double zoom = 1.0;
  bool pinned = false;
  int progressBucket = -1;
  bool suppressCommit = false;
  bool errorPageShown = false;
  bool crashedOnce = false;
  int navSeq = 0;
  // Deferred load (session restore): the tab exists but has no browser yet.
  std::string pendingUrl;
  bool creating = false;
  // {code,desc,url,kind} | null
  Json error;
  // Context-menu snapshot consumed by ctx:command (params + timestamp).
  Json lastCtx;
  // URL of the load this app requested (LoadURL); OnBeforeBrowse lets it
  // through the navigation policy (Electron's will-navigate does not fire
  // for programmatic loads).
  std::string expectedNav;

  // Memory saver (sleeping tabs): the browser was discarded to free the
  // renderer's memory while the strip entry (url/title/favicon) survives.
  // setActive reloads the URL on wake; savedScroll is re-applied after load.
  bool slept = false;
  double savedScroll = 0;
  int64_t lastActiveMs = 0;
  // CDP scroll/audio probe in flight (guards the discard decision).
  bool probeInFlight = false;
  int probeMsgId = 0;
  CefRefPtr<CefRegistration> devtoolsReg;

  bool hasBrowser() const { return client.get() != nullptr; }
};

class TabManager {
 public:
  using EmitFn = std::function<void(const std::string& channel,
                                    const Json& payload)>;

  TabManager(MainWindow* win, EmitFn emit);
  ~TabManager();

  TabManager(const TabManager&) = delete;
  TabManager& operator=(const TabManager&) = delete;

  MainWindow* window() const { return win_; }

  // --- mutation ------------------------------------------------------------
  Tab* create(const std::string& url, bool activate);
  Tab* createBackground(const std::string& url);
  void close(int id, bool destroyed = false);
  void setActive(int id);
  // Duplicate the tab with id `id` (opens a copy after it).
  Tab* duplicate(int id);
  // Reopen the most recently closed tab; returns the new tab or nullptr.
  Tab* reopenClosed();
  // Move tab `id` so it sits immediately before tab `beforeId`.
  void move(int id, int beforeId);
  // Toggle the pinned state of tab `id`; pinned tabs sort to the front.
  void togglePin(int id);
  bool load(Tab* tab, const std::string& url);
  void setBounds(int x, int y, int w, int h);
  void setPopupHidden(bool hidden);
  void setZoom(double factor);
  void setZoom(Tab* tab, double factor);
  void showError(Tab* tab, int code, const std::string& desc,
                 const std::string& url);

  // --- queries -------------------------------------------------------------
  Tab* get(int id);
  Tab* active();
  Json list() const;  // snapshot array
  int count() const { return static_cast<int>(tabs_.size()); }
  int indexOf(int id) const;  // -1 when not found
  void each(const std::function<void(Tab*)>& fn);
  Tab* findByBrowser(int browserId);
  Tab* findByHwnd(HWND h);

  // --- events (coalesced, one per message-loop turn) -----------------------
  void emitTabs();
  void navState();
  void navState(Tab* tab);

  // --- load lifecycle (called by Web98Client) ------------------------------
  void OnLoadingStateChange(Tab* tab, bool isLoading, bool canGoBack,
                            bool canGoForward);
  void OnProgress(Tab* tab, double progress);
  void OnAddressChange(Tab* tab, const std::string& url);
  void OnTitleChange(Tab* tab, const std::string& title);
  void OnFaviconChange(Tab* tab, const std::string& iconUrl);
  void OnLoadEnd(Tab* tab);   // main frame finished (or failed) loading
  void OnLoadError(Tab* tab, int code, const std::string& desc,
                   const std::string& url);

  // --- called by Web98Client ------------------------------------------------
  void OnTabCreated(Tab* tab, CefRefPtr<CefBrowser> browser);
  void OnTabClosed(Tab* tab);

  // --- memory saver (sleeping tabs) ----------------------------------------
  // CDP method result for a tab's sleep probe (scroll + audio playback);
  // called by the per-tab DevTools observer.
  void OnDevToolsResult(int tabId, int messageId, bool success,
                        const std::string& json);

  // Chrome-driven content rectangle (viewport of the chrome page).
  int boundsX() const { return boundsX_; }
  int boundsY() const { return boundsY_; }
  int boundsW() const { return boundsW_; }
  int boundsH() const { return boundsH_; }

 private:
  void wire(Tab* tab);
  bool ensureBrowser(Tab* tab, const std::string& initialUrl);
  void applyBounds(Tab* tab);
  void syncVisibility();
  void flushPending();
  void scheduleFlush(int delayMs);
  // Memory saver heartbeat: sleeps idle background tabs past the threshold.
  void scheduleSweep(int delayMs);
  void sweep();
  bool startSleepProbe(Tab* t);
  void sleepTab(Tab* t);
  static std::string zoneFor(const std::string& url);
  void commitHistory(Tab* tab);

  MainWindow* win_;
  EmitFn emit_;
  std::vector<std::unique_ptr<Tab>> tabs_;
  // Recently closed tabs (url+title), newest last. Max 10.
  struct ClosedTab {
    std::string url;
    std::string title;
  };
  std::vector<ClosedTab> closedTabs_;
  int activeId_ = 0;
  int nextId_ = 1;
  bool hiddenForPopup_ = false;

  int boundsX_ = 0, boundsY_ = 0, boundsW_ = 0, boundsH_ = 0;

  // Deferred background loads.
  struct Job {
    int tabId = 0;
    std::string url;
  };
  std::vector<Job> pendingLoads_;
  bool flushScheduled_ = false;
  std::vector<int> flushInflight_;  // tabs with a background load in flight

  // Memory saver sweep (self-perpetuating while the window lives).
  bool sweepScheduled_ = false;

  // Coalescing state.
  bool tabsEmitScheduled_ = false;
  bool navEmitScheduled_ = false;
  int pendingNavId_ = 0;
  std::string lastTabsJson_;
  std::string lastNavJson_;

  // Keeps posted callbacks from touching a destroyed manager.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace web98
