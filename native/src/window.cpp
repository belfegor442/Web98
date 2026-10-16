#include "window.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <map>

#include "app_core.h"
#include "base.h"
#include "cef_engine.h"
#include "cef_util.h"
#include "safeurl.h"
#include "settings.h"

namespace web98 {
namespace {

constexpr wchar_t kFrameClass[] = L"Web98Frame";
constexpr wchar_t kDialogClass[] = L"Web98Dialog";

struct ChildEntry {
  WNDPROC orig;
  HWND top;  // owning top-level window (frame or dialog)
};
std::map<HWND, ChildEntry> g_children;

bool isResizable(HWND hwnd) {
  return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;
}

// Screen-point -> non-client hit code for the invisible resize margin.
// Returns 0 when the point should stay a client point.
int edgeHit(HWND top, POINT pt) {
  if (!isResizable(top) || ::IsZoomed(top)) return 0;
  RECT rc;
  if (!::GetWindowRect(top, &rc)) return 0;
  const int f = 6;
  bool left = pt.x >= rc.left && pt.x < rc.left + f;
  bool right = pt.x <= rc.right && pt.x > rc.right - f;
  bool top_ = pt.y >= rc.top && pt.y < rc.top + f;
  bool bottom = pt.y <= rc.bottom && pt.y > rc.bottom - f;
  if (left && top_) return HTTOPLEFT;
  if (right && top_) return HTTOPRIGHT;
  if (left && bottom) return HTBOTTOMLEFT;
  if (right && bottom) return HTBOTTOMRIGHT;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  if (top_) return HTTOP;
  if (bottom) return HTBOTTOM;
  return 0;
}

// The CEF chrome/dialog child fills the whole top-level window, so resize
// hit-testing must be answered there and handed back to the frame window
// via HTTRANSPARENT (a child resizing itself would be wrong).
LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto it = g_children.find(hwnd);
  WNDPROC orig = it != g_children.end() ? it->second.orig : DefWindowProcW;
  HWND top = it != g_children.end() ? it->second.top : nullptr;

  switch (msg) {
    case WM_NCHITTEST: {
      if (top) {
        POINT pt = {static_cast<short>(LOWORD(lp)),
                    static_cast<short>(HIWORD(lp))};
        if (edgeHit(top, pt) != 0) return HTTRANSPARENT;
      }
      break;
    }
    case WM_SETCURSOR: {
      if (top && isResizable(top) && !::IsZoomed(top)) {
        // The frame already resolved the hit code; let it own the cursor.
        LRESULT r = ::SendMessageW(top, WM_SETCURSOR, wp, lp);
        if (r) return r;
      }
      break;
    }
    case WM_MOUSEWHEEL: {
      if (top && (wp & MK_CONTROL)) {
        MainWindow* mw = nullptr;
        for (auto& w : AppCore::get().mainWindows()) {
          if (w && w->hwnd() == top) { mw = w.get(); break; }
        }
        if (mw && mw->tabs()) {
          Tab* tab = mw->tabs()->findByHwnd(hwnd);
          if (tab) {
            double delta = static_cast<short>(HIWORD(wp)) > 0 ? 0.25 : -0.25;
            double z = std::min(3.0, std::max(0.25, tab->zoom + delta));
            mw->tabs()->setZoom(tab, z);
            return 0;
          }
        }
      }
      break;
    }
    case WM_NCDESTROY:
      g_children.erase(hwnd);
      break;
    default:
      break;
  }
  return ::CallWindowProcW(orig, hwnd, msg, wp, lp);
}

}  // namespace

void subclassChild(HWND child, HWND top) {
  if (!child || !top || g_children.count(child)) return;
  WNDPROC orig = reinterpret_cast<WNDPROC>(
      ::SetWindowLongPtrW(child, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(ChildProc)));
  if (orig) g_children[child] = ChildEntry{orig, top};
}

bool RegisterFrameClasses() {
  static bool done = false;
  if (done) return true;
  done = true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = MainWindow::WndProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = ::CreateSolidBrush(RGB(0xc0, 0xc0, 0xc0));
  wc.lpszClassName = kFrameClass;
  if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;
  wc.lpfnWndProc = DialogWindow::WndProc;
  wc.lpszClassName = kDialogClass;
  return ::RegisterClassExW(&wc) != 0 ||
         ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

/* ------------------------------ MainWindow -------------------------------- */

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() {
  if (chromeClient_) {
    CefRefPtr<CefBrowser> b = chromeClient_->browser();
    chromeClient_->Detach();
    if (b) b->GetHost()->CloseBrowser(true);
    chromeClient_ = nullptr;
  }
  chrome_ = nullptr;
  tabs_.reset();
}

bool MainWindow::create(const SessionWindow* session, bool show) {
  if (!RegisterFrameClasses()) return false;
  if (session) {
    restoreUrls_ = session->urls;
    restoreActive_ = session->active;
  }
  showWhenReady_ = show;

  DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_SYSMENU |
                WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
  int w = 1280, h = 860;
  ::CreateWindowExW(0, kFrameClass, L"Web98", style, CW_USEDEFAULT,
                    CW_USEDEFAULT, w, h, nullptr, nullptr,
                    ::GetModuleHandleW(nullptr), this);
  if (!hwnd_) return false;
  ::DragAcceptFiles(hwnd_, TRUE);

  tabs_ = std::make_unique<TabManager>(this, [this](const std::string& ch,
                                                    const Json& p) {
    emit(ch, p);
  });

  // The chrome UI child covers the whole frame; tab children are stacked on
  // top of it by TabManager using the bounds the chrome page reports.
  std::string url = pathToFileUrl(pathJoin(resourceDir(), "index.html"));
  RECT rc;
  ::GetClientRect(hwnd_, &rc);
  CefWindowInfo wi;
  wi.SetAsChild(hwnd_, CefRect(0, 0, rc.right - rc.left, rc.bottom - rc.top));
  wi.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  BrowserCtx ctx;
  ctx.role = BrowserCtx::kChrome;
  ctx.window = this;
  ctx.tabs = tabs_.get();
  chromeClient_ = new Web98Client(ctx, url);
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 0xc0, 0xc0, 0xc0);
  if (!CefBrowserHost::CreateBrowser(wi, chromeClient_, url, settings, nullptr,
                                     nullptr)) {
    chromeClient_ = nullptr;
    // Destroy the orphan HWND so GWLP_USERDATA does not dangle.
    HWND orphan = hwnd_;
    hwnd_ = nullptr;
    if (orphan) ::DestroyWindow(orphan);
    return false;
  }
  return true;
}

void MainWindow::setPendingSession(const SessionWindow* session, bool show) {
  if (session) {
    restoreUrls_ = session->urls;
    restoreActive_ = session->active;
  }
  showWhenReady_ = show;
}

void MainWindow::attachChrome(CefRefPtr<CefBrowser> browser) {
  chrome_ = browser;
  if (!hwnd_ || !browser || !browser->GetHost()) return;
  HWND child = reinterpret_cast<HWND>(browser->GetHost()->GetWindowHandle());
  subclassChild(child, hwnd_);
}

void MainWindow::emit(const std::string& channel, const Json& payload) {
  if (chrome_ && chrome_->GetMainFrame()) {
    SendIpcEvent(chrome_->GetMainFrame(), channel, payload);
  }
}

void MainWindow::setTitle(const std::string& title) {
  if (!hwnd_) return;
  std::wstring w = toWide(title.empty() ? "Web98" : title);
  ::SetWindowTextW(hwnd_, w.c_str());
}

void MainWindow::minimize() {
  if (hwnd_) ::SendMessageW(hwnd_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

bool MainWindow::isMaximized() const {
  return hwnd_ && ::IsZoomed(hwnd_) != 0;
}

bool MainWindow::toggleMaximize() {
  if (!hwnd_) return false;
  ::SendMessageW(hwnd_, WM_SYSCOMMAND,
                 isMaximized() ? SC_RESTORE : SC_MAXIMIZE, 0);
  return isMaximized();
}

void MainWindow::setFullscreen(bool on) {
  if (!hwnd_ || on == fullscreen_) return;
  LONG_PTR style = ::GetWindowLongPtrW(hwnd_, GWL_STYLE);
  if (on) {
    restoreBounds_ = {0, 0, 0, 0};
    ::GetWindowRect(hwnd_, &restoreBounds_);
    ::SetWindowLongPtrW(hwnd_, GWL_STYLE,
                        style & ~(WS_THICKFRAME | WS_CAPTION | WS_SYSMENU |
                                  WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
    HMONITOR mon = ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(mon, &mi)) {
      ::SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
  } else {
    ::SetWindowLongPtrW(hwnd_, GWL_STYLE,
                        style | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU |
                            WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    ::SetWindowPos(hwnd_, HWND_TOP, restoreBounds_.left, restoreBounds_.top,
                   restoreBounds_.right - restoreBounds_.left,
                   restoreBounds_.bottom - restoreBounds_.top,
                   SWP_FRAMECHANGED | SWP_NOACTIVATE);
  }
  fullscreen_ = on;
  emit("win:state", [&] {
    Json o = Json::makeObject();
    o.obj.emplace_back("maximized", Json::makeBool(on ? false : isMaximized()));
    return o;
  }());
}

void MainWindow::focus() {
  if (!hwnd_) return;
  if (::IsIconic(hwnd_)) ::ShowWindow(hwnd_, SW_RESTORE);
  ::SetForegroundWindow(hwnd_);
  Tab* t = tabs_ ? tabs_->active() : nullptr;
  if (t && t->client && t->client->browser() && t->client->browser()->GetHost()) {
    t->client->browser()->GetHost()->SetFocus(true);
  } else if (chrome_ && chrome_->GetHost()) {
    chrome_->GetHost()->SetFocus(true);
  }
}

void MainWindow::requestClose() {
  if (hwnd_) ::SendMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void MainWindow::onChromeLoaded() {
  if (chromeLoaded_) return;
  chromeLoaded_ = true;

  std::vector<std::string> urls = sanitizeSessionList(restoreUrls_, 40);
  restoreUrls_.clear();
  if (!urls.empty()) {
    int activeIdx = restoreActive_;
    if (activeIdx < 0 || activeIdx >= static_cast<int>(urls.size())) activeIdx = 0;
    for (size_t i = 0; i < urls.size(); ++i) {
      if (static_cast<int>(i) == activeIdx) tabs_->create(urls[i], true);
      else tabs_->createBackground(urls[i]);
    }
    if (tabs_->count() > 0) {
      Json list = tabs_->list();
      if (activeIdx < static_cast<int>(list.arr.size())) {
        const Json* id = list.arr[activeIdx].find("id");
        if (id && id->isNumber()) tabs_->setActive(static_cast<int>(id->num));
      }
    }
  } else {
    tabs_->create(AppCore::get().settingsModel().getString("homepage",
                                                          homepageUrl()),
                  true);
  }

  if (showWhenReady_) ::ShowWindow(hwnd_, SW_SHOW);
  ::UpdateWindow(hwnd_);

  Json state = Json::makeObject();
  state.obj.emplace_back("maximized", Json::makeBool(isMaximized()));
  emit("win:state", state);
  Json active = Json::makeObject();
  active.obj.emplace_back("active",
                          Json::makeBool(::GetForegroundWindow() == hwnd_));
  emit("win:active", active);

  Json ready = Json::makeObject();
  int64_t started = AppCore::get().startupAtMs();
  ready.obj.emplace_back("startupMs",
                         Json::makeNumber(static_cast<double>(nowMs() - started)));
  emit("app:ready", ready);
}

void MainWindow::layoutChildren() {
  if (!hwnd_) return;
  RECT rc;
  ::GetClientRect(hwnd_, &rc);
  HWND child = nullptr;
  if (chrome_ && chrome_->GetHost()) {
    child = reinterpret_cast<HWND>(chrome_->GetHost()->GetWindowHandle());
  }
  if (child) {
    ::MoveWindow(child, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
  }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
  MainWindow* self = reinterpret_cast<MainWindow*>(
      ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    self = static_cast<MainWindow*>(cs->lpCreateParams);
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    if (self) self->hwnd_ = hwnd;
  }

  switch (msg) {
    case WM_NCCALCSIZE: {
      if (wp) {
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        if (::IsZoomed(hwnd)) {
          HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
          MONITORINFO mi{};
          mi.cbSize = sizeof(mi);
          if (::GetMonitorInfoW(mon, &mi)) {
            RECT& r = p->rgrc[0];
            if (r.left < mi.rcWork.left) r.left = mi.rcWork.left;
            if (r.top < mi.rcWork.top) r.top = mi.rcWork.top;
            if (r.right > mi.rcWork.right) r.right = mi.rcWork.right;
            if (r.bottom > mi.rcWork.bottom) r.bottom = mi.rcWork.bottom;
          }
        }
        return 0;  // frameless: the client area is the whole window
      }
      break;
    }
    case WM_GETMINMAXINFO: {
      if (!self || self->fullscreen_) break;
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi{};
      mi.cbSize = sizeof(mi);
      if (::GetMonitorInfoW(mon, &mi)) {
        // Maximize to the work area so the taskbar stays reachable.
        mmi->ptMaxPosition.x = mi.rcWork.left;
        mmi->ptMaxPosition.y = mi.rcWork.top;
        mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
        mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
      }
      return 0;
    }
    case WM_NCHITTEST: {
      POINT pt = {static_cast<short>(LOWORD(lp)),
                  static_cast<short>(HIWORD(lp))};
      int hit = edgeHit(hwnd, pt);
      if (hit) return hit;
      return HTCLIENT;
    }
    case WM_SIZE: {
      if (self) {
        self->layoutChildren();
        if (wp == SIZE_MAXIMIZED || wp == SIZE_RESTORED) {
          Json o = Json::makeObject();
          o.obj.emplace_back("maximized", Json::makeBool(::IsZoomed(hwnd) != 0));
          self->emit("win:state", o);
        }
      }
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_ACTIVATE: {
      if (self) {
        Json o = Json::makeObject();
        o.obj.emplace_back("active", Json::makeBool(LOWORD(wp) != WA_INACTIVE));
        self->emit("win:active", o);
      }
      break;
    }
    case WM_CLOSE: {
      if (!AppCore::get().quitting()) AppCore::get().saveSession(self);
      ::DestroyWindow(hwnd);
      return 0;
    }
    case WM_DESTROY: {
      if (self) AppCore::get().retireMainWindow(self);
      return 0;
    }
    case WM_DROPFILES: {
      if (self && self->tabs_) {
        HDROP drop = reinterpret_cast<HDROP>(wp);
        UINT n = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        // First file activates; the rest open as background tabs.
        for (UINT i = 0; i < n; ++i) {
          UINT len = ::DragQueryFileW(drop, i, nullptr, 0);
          if (!len) continue;
          std::wstring buf(len + 1, L'\0');
          ::DragQueryFileW(drop, i, &buf[0], len + 1);
          buf.resize(len);
          std::string path = toUtf8(buf);
          if (path.empty()) continue;
          std::string url = pathToFileUrl(path);
          if (i == 0) self->tabs_->create(url, true);
          else self->tabs_->createBackground(url);
        }
      }
      ::DragFinish(reinterpret_cast<HDROP>(wp));
      return 0;
    }
    case WM_NCDESTROY:
      if (self) self->hwnd_ = nullptr;
      break;
    default:
      break;
  }
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------ DialogWindow ------------------------------- */

namespace {
struct DialogSpec {
  const char* title;
  int w, h;
  bool resizable;
};
const DialogSpec* specFor(const std::string& page) {
  static const std::map<std::string, DialogSpec> specs = {
      {"about", {"About Web98", 400, 240, false}},
      {"source", {"Source", 720, 520, true}},
      {"favorites", {"Organize Favorites", 540, 440, false}},
      {"history", {"History", 580, 470, false}},
      {"downloads", {"Downloads", 580, 420, false}},
      {"pageinfo", {"Page Information", 480, 420, false}},
      {"settings", {"Options", 540, 520, false}},
  };
  auto it = specs.find(page);
  return it == specs.end() ? nullptr : &it->second;
}
}  // namespace

DialogWindow::DialogWindow() = default;
DialogWindow::~DialogWindow() {
  if (client_) {
    CefRefPtr<CefBrowser> b = client_->browser();
    client_->Detach();
    if (b) b->GetHost()->CloseBrowser(true);
    client_ = nullptr;
  }
  browser_ = nullptr;
}

bool DialogWindow::create(MainWindow* parent, const std::string& page,
                          const Json* payload) {
  const DialogSpec* spec = specFor(page);
  if (!spec || !RegisterFrameClasses()) return false;
  page_ = page;
  parent_ = parent;
  if (payload) {
    payload_ = *payload;
    pendingPayload_ = true;
  }

  DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_SYSMENU;
  if (spec->resizable) style |= WS_THICKFRAME;

  int w = spec->w, h = spec->h;
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  if (parent && parent->hwnd()) {
    RECT pr;
    ::GetWindowRect(parent->hwnd(), &pr);
    x = pr.left + ((pr.right - pr.left) - w) / 2;
    y = pr.top + ((pr.bottom - pr.top) - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
  }

  std::wstring title = toWide(spec->title);
  ::CreateWindowExW(0, kDialogClass, title.c_str(), style, x, y, w, h, nullptr,
                    nullptr, ::GetModuleHandleW(nullptr), this);
  if (!hwnd_) return false;

  std::string url =
      pathToFileUrl(pathJoin(pathJoin(resourceDir(), "dialogs"), page + ".html"));
  RECT rc;
  ::GetClientRect(hwnd_, &rc);
  CefWindowInfo wi;
  wi.SetAsChild(hwnd_, CefRect(0, 0, rc.right - rc.left, rc.bottom - rc.top));
  wi.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  BrowserCtx ctx;
  ctx.role = BrowserCtx::kDialog;
  ctx.dialog = this;
  client_ = new Web98Client(ctx, url);
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 0xc0, 0xc0, 0xc0);
  if (!CefBrowserHost::CreateBrowser(wi, client_, url, settings, nullptr, nullptr)) {
    client_ = nullptr;
    // Destroy the orphan HWND so GWLP_USERDATA does not dangle.
    HWND orphan = hwnd_;
    hwnd_ = nullptr;
    if (orphan) ::DestroyWindow(orphan);
    return false;
  }
  return true;
}

void DialogWindow::attachBrowser(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (!hwnd_ || !browser || !browser->GetHost()) return;
  HWND child = reinterpret_cast<HWND>(browser->GetHost()->GetWindowHandle());
  subclassChild(child, hwnd_);
}

void DialogWindow::emit(const std::string& channel, const Json& payload) {
  if (browser_ && browser_->GetMainFrame()) {
    SendIpcEvent(browser_->GetMainFrame(), channel, payload);
  }
}

void DialogWindow::setPayload(const Json& payload) {
  payload_ = payload;
  if (loaded_) emit("payload", payload_);
  else pendingPayload_ = true;
}

void DialogWindow::onLoaded() {
  loaded_ = true;
  if (pendingPayload_) {
    pendingPayload_ = false;
    emit("payload", payload_);
  }
  if (parent_ && parent_->hwnd()) {
    RECT pr;
    ::GetWindowRect(parent_->hwnd(), &pr);
    RECT dr;
    ::GetWindowRect(hwnd_, &dr);
    int w = dr.right - dr.left, h = dr.bottom - dr.top;
    ::SetWindowPos(hwnd_, HWND_TOP,
                   pr.left + ((pr.right - pr.left) - w) / 2,
                   pr.top + ((pr.bottom - pr.top) - h) / 2, 0, 0,
                   SWP_NOSIZE | SWP_NOACTIVATE);
  }
  ::ShowWindow(hwnd_, SW_SHOW);
  ::UpdateWindow(hwnd_);
  Json active = Json::makeObject();
  active.obj.emplace_back("active", Json::makeBool(true));
  emit("win:active", active);
}

void DialogWindow::layoutChildren() {
  if (!hwnd_) return;
  RECT rc;
  ::GetClientRect(hwnd_, &rc);
  HWND child = nullptr;
  if (browser_ && browser_->GetHost()) {
    child = reinterpret_cast<HWND>(browser_->GetHost()->GetWindowHandle());
  }
  if (child) {
    ::MoveWindow(child, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
  }
}

void DialogWindow::requestClose() {
  if (hwnd_) ::SendMessageW(hwnd_, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK DialogWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                       LPARAM lp) {
  DialogWindow* self = reinterpret_cast<DialogWindow*>(
      ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    self = static_cast<DialogWindow*>(cs->lpCreateParams);
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    if (self) self->hwnd_ = hwnd;
  }

  switch (msg) {
    case WM_NCCALCSIZE:
      if (wp) return 0;
      break;
    case WM_NCHITTEST: {
      POINT pt = {static_cast<short>(LOWORD(lp)),
                  static_cast<short>(HIWORD(lp))};
      int hit = edgeHit(hwnd, pt);
      if (hit) return hit;
      return HTCLIENT;
    }
    case WM_SIZE:
      if (self) self->layoutChildren();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_ACTIVATE: {
      if (self) {
        Json o = Json::makeObject();
        o.obj.emplace_back("active", Json::makeBool(LOWORD(wp) != WA_INACTIVE));
        self->emit("win:active", o);
      }
      break;
    }
    case WM_CLOSE:
      ::DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (self) AppCore::get().removeDialog(self);
      return 0;
    case WM_NCDESTROY:
      if (self) self->hwnd_ = nullptr;
      break;
    default:
      break;
  }
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace web98
