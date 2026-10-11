#pragma once
// Frameless Win32 windows hosting CEF child browsers.
//   MainWindow  - retro chrome UI (index.html) filling the client area, plus
//                 the stacked tab children managed by TabManager.
//   DialogWindow - one dialogs/<page>.html child browser.
#include <memory>
#include <string>
#include <vector>

#include "cef_client.h"
#include "json.h"
#include "session.h"

namespace web98 {

class TabManager;

class MainWindow {
 public:
  MainWindow();
  ~MainWindow();

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  // Frameless window + chrome browser. `session` may be null (fresh window).
  bool create(const SessionWindow* session, bool show);

  HWND hwnd() const { return hwnd_; }
  TabManager* tabs() { return tabs_.get(); }
  CefRefPtr<CefBrowser> chrome() const { return chrome_; }

  // Send an event to the chrome renderer (index.html).
  void emit(const std::string& channel, const Json& payload);

  void setTitle(const std::string& title);
  void minimize();
  bool isMaximized() const;
  bool toggleMaximize();          // returns the new maximized state
  void setFullscreen(bool on);
  bool isFullscreen() const { return fullscreen_; }
  void focus();
  void requestClose();            // graceful close (WM_CLOSE)

  // Called by the chrome client once index.html finished loading.
  void onChromeLoaded();
  // Called by AppCore right after create() to carry the session over.
  void setPendingSession(const SessionWindow* session, bool show);
  // Called from Web98Client::OnAfterCreated (kChrome role).
  void attachChrome(CefRefPtr<CefBrowser> browser);

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

 private:
  void layoutChildren();

  HWND hwnd_ = nullptr;
  std::unique_ptr<TabManager> tabs_;
  CefRefPtr<Web98Client> chromeClient_;
  CefRefPtr<CefBrowser> chrome_;
  std::vector<std::string> restoreUrls_;
  int restoreActive_ = 0;
  bool showWhenReady_ = true;
  bool chromeLoaded_ = false;
  bool fullscreen_ = false;
  RECT restoreBounds_ = {0, 0, 0, 0};
};

class DialogWindow {
 public:
  DialogWindow();
  ~DialogWindow();

  DialogWindow(const DialogWindow&) = delete;
  DialogWindow& operator=(const DialogWindow&) = delete;

  // page must be one of DIALOG_PAGES; parent may be null.
  bool create(MainWindow* parent, const std::string& page,
              const Json* payload);

  HWND hwnd() const { return hwnd_; }
  const std::string& page() const { return page_; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }

  void emit(const std::string& channel, const Json& payload);
  void setPayload(const Json& payload);  // queued until the page loaded
  void requestClose();
  // Called from Web98Client::OnAfterCreated (kDialog role) / OnLoadEnd.
  void attachBrowser(CefRefPtr<CefBrowser> browser);
  void onLoaded();

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

 private:
  void layoutChildren();

  HWND hwnd_ = nullptr;
  std::string page_;
  MainWindow* parent_ = nullptr;
  CefRefPtr<Web98Client> client_;
  CefRefPtr<CefBrowser> browser_;
  bool pendingPayload_ = false;
  bool loaded_ = false;
  Json payload_;
};

// Registers the window classes (idempotent).
bool RegisterFrameClasses();

}  // namespace web98
