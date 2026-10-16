#include "cef_client.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "app_core.h"
#include "base.h"
#include "cef_engine.h"
#include "cef_util.h"
#include "dialogs.h"
#include "menus.h"
#include "safeurl.h"
#include "security.h"
#include "settings.h"
#include "window.h"

// gdiplus last: it needs windows.h (already pulled in) and must not clobber
// min/max macros used by headers above.
#pragma warning(push, 0)
#undef min
#undef max
#include <objidl.h>
#include <gdiplus.h>
#pragma warning(pop)

namespace web98 {
namespace {

// Programmatic loads (LoadURL) and history navigations must bypass the
// navigation policy exactly like Electron, which does not emit will-navigate
// for loadURL()/back()/forward().
bool urlsMatch(const std::string& a, const std::string& b) {
  if (a == b) return true;
  auto norm = [](std::string s) {
    size_t hash = s.find('#');
    if (hash != std::string::npos) s.resize(hash);
    size_t scheme = s.find(':');
    if (scheme != std::string::npos) {
      for (size_t i = 0; i < scheme; ++i) {
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = static_cast<char>(s[i] - 'A' + 'a');
      }
    }
    while (s.size() > 8 && s.back() == '/') s.pop_back();
    return s;
  };
  return norm(a) == norm(b);
}

std::string mediaTypeName(CefContextMenuParams::MediaType type) {
  switch (type) {
    case CM_MEDIATYPE_IMAGE: return "image";
    case CM_MEDIATYPE_VIDEO: return "video";
    case CM_MEDIATYPE_AUDIO: return "audio";
    case CM_MEDIATYPE_CANVAS: return "canvas";
    case CM_MEDIATYPE_FILE: return "file";
    case CM_MEDIATYPE_PLUGIN: return "plugin";
    case CM_MEDIATYPE_NONE:
    default: return "none";
  }
}

std::string base64Encode(const unsigned char* data, size_t len) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 2 < len) {
    unsigned n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out += tbl[(n >> 18) & 63];
    out += tbl[(n >> 12) & 63];
    out += tbl[(n >> 6) & 63];
    out += tbl[n & 63];
    i += 3;
  }
  if (i < len) {
    unsigned n = static_cast<unsigned>(data[i]) << 16;
    out += tbl[(n >> 18) & 63];
    if (i + 1 < len) {
      n |= static_cast<unsigned>(data[i + 1]) << 8;
      out += tbl[(n >> 12) & 63];
      out += tbl[(n >> 6) & 63];
      out += '=';
    } else {
      out += tbl[(n >> 12) & 63];
      out += "==";
    }
  }
  return out;
}

CLSID pngEncoderClsid() {
  CLSID id = {};
  UINT num = 0, size = 0;
  if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || !size)
    return id;
  std::vector<unsigned char> buf(size);
  auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
  if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return id;
  for (UINT i = 0; i < num; ++i) {
    if (info[i].MimeType && wcscmp(info[i].MimeType, L"image/png") == 0)
      return info[i].Clsid;
  }
  return id;
}

// Freeze `hwnd` into a PNG data URL via PrintWindow + GDI+. Returns empty
// string on any failure (caller falls back to no snapshot / white ghost).
// The context menu is HTML inside the chrome page, which sits BELOW the tab
// HWND; to reveal it the tab is hidden, so we paint this frozen snapshot on
// the ghost behind the menu instead of exposing the white background.
std::string captureHwndPngDataUrl(HWND hwnd) {
  if (!hwnd || !::IsWindow(hwnd)) return std::string();
  RECT rc;
  if (!::GetClientRect(hwnd, &rc)) return std::string();
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return std::string();

  HDC screen = ::GetDC(nullptr);
  if (!screen) return std::string();
  HDC mem = ::CreateCompatibleDC(screen);
  HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
  HGDIOBJ old = bmp ? ::SelectObject(mem, bmp) : nullptr;
  BOOL ok = old && ::PrintWindow(hwnd, mem, 2 /* PW_RENDERFULLCONTENT */);
  if (old) ::SelectObject(mem, old);
  ::ReleaseDC(nullptr, screen);
  ::DeleteDC(mem);
  if (!ok || !bmp) {
    if (bmp) ::DeleteObject(bmp);
    return std::string();
  }

  static ULONG_PTR s_token = 0;
  static bool s_initTried = false;
  if (!s_initTried) {
    s_initTried = true;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&s_token, &input, nullptr) != Gdiplus::Ok)
      s_token = 0;
  }

  std::string url;
  if (s_token) {
    Gdiplus::Bitmap src(bmp, nullptr);
    IStream* stream = nullptr;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK && stream) {
      CLSID pngId = pngEncoderClsid();
      if (src.Save(stream, &pngId, nullptr) == Gdiplus::Ok) {
        HGLOBAL hg = nullptr;
        if (::GetHGlobalFromStream(stream, &hg) == S_OK && hg) {
          SIZE_T sz = ::GlobalSize(hg);
          const char* p = static_cast<const char*>(::GlobalLock(hg));
          if (p && sz > 0) {
            url = "data:image/png;base64,";
            url += base64Encode(reinterpret_cast<const unsigned char*>(p),
                                sz);
          }
          if (p) ::GlobalUnlock(hg);
        }
      }
      stream->Release();
    }
  }
  ::DeleteObject(bmp);
  return url;
}

}  // namespace

void SendIpcReply(CefRefPtr<CefFrame> frame, int64_t id, bool ok,
                  const Json& result) {
  if (!frame) return;
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("web98:reply");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetDouble(0, static_cast<double>(id));
  args->SetBool(1, ok);
  args->SetString(2, result.dump());
  frame->SendProcessMessage(PID_RENDERER, msg);
}

void SendIpcEvent(CefRefPtr<CefFrame> frame, const std::string& channel,
                  const Json& payload) {
  if (!frame) return;
  CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("web98:event");
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, channel);
  args->SetString(1, payload.dump());
  frame->SendProcessMessage(PID_RENDERER, msg);
}

Web98Client::Web98Client(const BrowserCtx& ctx, const std::string& initialUrl)
    : ctx_(ctx), initialUrl_(initialUrl) {}

Web98Client::~Web98Client() = default;

void Web98Client::Detach() {
  ctx_ = BrowserCtx();
  browser_ = nullptr;
  detached_ = true;
}

void Web98Client::SendEvent(const std::string& channel, const Json& payload) {
  if (!browser_) return;
  SendIpcEvent(browser_->GetMainFrame(), channel, payload);
}

/* ------------------------------ IPC (browser) ---------------------------- */

bool Web98Client::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  if (detached_) {
    return false;
  }
  if (source_process != PID_RENDERER) return false;
  if (message->GetName() != "web98:ipc") return false;
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (args->GetSize() < 4) return true;
  bool isInvoke = args->GetBool(0);
  int64_t id = static_cast<int64_t>(args->GetDouble(1));
  std::string channel = str(args->GetString(2));
  Json arg;
  if (!Json::parse(str(args->GetString(3)), arg)) arg = Json::makeNull();
  AppCore::get().handleIpc(ctx_, channel, arg, isInvoke, id, frame);
  return true;
}

/* ---------------------------- context menu -------------------------------- */

bool Web98Client::RunContextMenu(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefContextMenuParams> params,
                                 CefRefPtr<CefMenuModel> model,
                                 CefRefPtr<CefRunContextMenuCallback> callback) {
  if (ctx_.role != BrowserCtx::kTab) {
    // Electron shows no context menu on the chrome/dialog pages, and CEF's
    // DevTools menu is not routed through this handler.
    if (ctx_.window || ctx_.dialog) {
      callback->Cancel();
      return true;
    }
    return false;
  }
  Tab* tab = ctx_.tab;
  if (!tab) return false;

  Json p = Json::makeObject();
  p.obj.emplace_back("x", Json::makeNumber(params->GetXCoord()));
  p.obj.emplace_back("y", Json::makeNumber(params->GetYCoord()));
  p.obj.emplace_back("linkURL", Json::makeString(str(params->GetLinkUrl())));
  p.obj.emplace_back("srcURL", Json::makeString(str(params->GetSourceUrl())));
  p.obj.emplace_back("selectionText",
                     Json::makeString(str(params->GetSelectionText())));
  p.obj.emplace_back("isEditable", Json::makeBool(params->IsEditable()));
  p.obj.emplace_back("mediaType",
                     Json::makeString(mediaTypeName(params->GetMediaType())));

  bool canBack = false, canForward = false;
  if (browser) {
    canBack = browser->CanGoBack();
    canForward = browser->CanGoForward();
  }

  Json store = Json::makeObject();
  store.obj.emplace_back("params", p);
  store.obj.emplace_back("time", Json::makeNumber(static_cast<double>(nowMs())));
  tab->lastCtx = store;

  Json menu = Json::makeObject();
  // CEF view coords are DIP relative to the tab browser's client area.
  // Convert to screen physical pixels, then to chrome-page CSS via DPR.
  int cefX = params->GetXCoord();
  int cefY = params->GetYCoord();
  HWND tabHwnd = nullptr;
  {
    if (tab->client && tab->client->browser()) {
      auto host = tab->client->browser()->GetHost();
      if (host) tabHwnd = host->GetWindowHandle();
    }
    HWND chromeHwnd = nullptr;
    if (ctx_.window && ctx_.window->chrome() && ctx_.window->chrome()->GetHost()) {
      chromeHwnd = ctx_.window->chrome()->GetHost()->GetWindowHandle();
    }
    if (tabHwnd && chromeHwnd) {
      UINT tabDpi = ::GetDpiForWindow(tabHwnd);
      double tabScale = tabDpi ? tabDpi / 96.0 : 1.0;
      POINT pt = {static_cast<LONG>(cefX * tabScale),
                  static_cast<LONG>(cefY * tabScale)};
      ::ClientToScreen(tabHwnd, &pt);
      ::ScreenToClient(chromeHwnd, &pt);
      // Send physical pixels relative to chrome client; JS divides by
      // window.devicePixelRatio to get CSS coordinates.
      cefX = pt.x;
      cefY = pt.y;
    }
  }
  // Freeze the tab's current pixels so the chrome page's content-ghost can
  // show them behind the menu once the tab HWND is hidden (the menu is HTML
  // inside the chrome page, which sits below the tab HWND).
  if (tabHwnd) {
    std::string bg = captureHwndPngDataUrl(tabHwnd);
    if (!bg.empty()) menu.obj.emplace_back("bg", Json::makeString(bg));
  }
  menu.obj.emplace_back("x", Json::makeNumber(cefX));
  menu.obj.emplace_back("y", Json::makeNumber(cefY));
  menu.obj.emplace_back("items", contextTemplate(p, canBack, canForward, tab->loading));
  // Electron's `emit` on a tab state targets that window's chrome renderer.
  if (ctx_.window) ctx_.window->emit("ctx", menu);
  callback->Cancel();
  return true;
}

/* ------------------------------ file dialogs ----------------------------- */

bool Web98Client::OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode,
                               const CefString& title,
                               const CefString& default_file_path,
                               const std::vector<CefString>& accept_filters,
                               const std::vector<CefString>& accept_extensions,
                               const std::vector<CefString>& accept_descriptions,
                               CefRefPtr<CefFileDialogCallback> callback) {
  HWND owner = reinterpret_cast<HWND>(browser->GetHost()->GetWindowHandle());
  std::vector<std::string> exts;
  for (const auto& f : accept_filters) {
    std::string s = str(f);
    if (!s.empty() && s[0] == '.') exts.push_back(s.substr(1));
  }
  for (const auto& e : accept_extensions) {
    std::string s = str(e);
    if (!s.empty() && s[0] == '.') s = s.substr(1);
    if (!s.empty() && std::find(exts.begin(), exts.end(), s) == exts.end())
      exts.push_back(s);
  }

  std::vector<CefString> paths;
  switch (mode) {
    case FILE_DIALOG_SAVE: {
      std::string p = dialogs::saveFileDialog(owner, str(title),
                                              str(default_file_path), exts);
      if (!p.empty()) paths.push_back(CefString(p));
      break;
    }
    case FILE_DIALOG_OPEN_FOLDER: {
      std::string p = dialogs::folderDialog(owner, str(title));
      if (!p.empty()) paths.push_back(CefString(p));
      break;
    }
    case FILE_DIALOG_OPEN_MULTIPLE: {
      std::vector<std::string> list = dialogs::openFilesDialog(owner, str(title),
                                                               exts);
      for (const auto& p : list) paths.push_back(CefString(p));
      break;
    }
    case FILE_DIALOG_OPEN:
    default: {
      std::string p = dialogs::openFileDialog(owner, str(title), exts);
      if (!p.empty()) paths.push_back(CefString(p));
      break;
    }
  }
  if (paths.empty()) callback->Cancel();
  else callback->Continue(paths);
  return true;
}

/* ------------------------------- display --------------------------------- */

void Web98Client::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  const CefString& url) {
  if (detached_) {
    return;
  }
  if (ctx_.role != BrowserCtx::kTab || !ctx_.tab || !ctx_.tabs) return;
  if (!frame->IsMain()) return;
  ctx_.tabs->OnAddressChange(ctx_.tab, str(url));
}

void Web98Client::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                const CefString& title) {
  if (ctx_.role == BrowserCtx::kTab && ctx_.tab && ctx_.tabs) {
    ctx_.tabs->OnTitleChange(ctx_.tab, str(title));
  } else if (ctx_.window) {
    ctx_.window->setTitle(sanitizeText(str(title), 200) + " - Web98");
  }
}

void Web98Client::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                     const std::vector<CefString>& icon_urls) {
  if (ctx_.role != BrowserCtx::kTab || !ctx_.tab || !ctx_.tabs) return;
  if (icon_urls.empty()) return;
  ctx_.tabs->OnFaviconChange(ctx_.tab, str(icon_urls[0]));
}

void Web98Client::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                                         bool fullscreen) {
  if (ctx_.window) ctx_.window->setFullscreen(fullscreen);
}

void Web98Client::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                                          double progress) {
  if (ctx_.role != BrowserCtx::kTab || !ctx_.tab || !ctx_.tabs) return;
  ctx_.tabs->OnProgress(ctx_.tab, progress);
}

/* ------------------------------- downloads -------------------------------- */

bool Web98Client::CanDownload(CefRefPtr<CefBrowser> browser,
                              const CefString& url,
                              const CefString& request_method) {
  return true;
}

bool Web98Client::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefDownloadItem> download_item,
                                   const CefString& suggested_name,
                                   CefRefPtr<CefBeforeDownloadCallback> callback) {
  std::string path;
  AppCore::get().registerCefDownload(download_item->GetId(),
                                     str(download_item->GetURL()),
                                     str(suggested_name), &path);
  callback->Continue(path, false);
  return true;
}

void Web98Client::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefDownloadItem> download_item,
                                    CefRefPtr<CefDownloadItemCallback> callback) {
  AppCore::get().updateCefDownload(download_item, callback);
}

/* --------------------------------- find ------------------------------------ */

void Web98Client::OnFindResult(CefRefPtr<CefBrowser> browser, int identifier,
                               int count, const CefRect& selection_rect,
                               int active_match, bool final_update) {
  if (!final_update) return;
  Json payload = Json::makeObject();
  payload.obj.emplace_back("count", Json::makeNumber(count));
  payload.obj.emplace_back("active", Json::makeNumber(active_match));
  // The find runs in the tab's browser; forward to the chrome page.
  if (ctx_.window) ctx_.window->emit("chrome:find-result", payload);
  else if (ctx_.role == BrowserCtx::kChrome)
    SendEvent("chrome:find-result", payload);
}

/* -------------------------------- focus ----------------------------------- */

bool Web98Client::OnSetFocus(CefRefPtr<CefBrowser> browser, FocusSource source) {
  if (ctx_.role == BrowserCtx::kTab && ctx_.window) {
    ctx_.window->emit("chrome:tab-focused", Json::makeNull());
  }
  return false;  // allow default focus handling
}

/* ------------------------------ js dialogs -------------------------------- */

bool Web98Client::OnJSDialog(CefRefPtr<CefBrowser> browser,
                             const CefString& origin_url,
                             JSDialogType dialog_type,
                             const CefString& message_text,
                             const CefString& default_prompt_text,
                             CefRefPtr<CefJSDialogCallback> callback,
                             bool& suppress_message) {
  suppress_message = true;
  HWND owner = reinterpret_cast<HWND>(browser->GetHost()->GetWindowHandle());
  std::string message = str(message_text);
  if (dialog_type == JSDIALOGTYPE_ALERT) {
    dialogs::messageBox(owner, message, "Web98", MB_OK | MB_ICONINFORMATION);
    callback->Continue(true, std::string());
    return true;
  }
  if (dialog_type == JSDIALOGTYPE_CONFIRM) {
    int r = dialogs::messageBox(owner, message, "Web98",
                                MB_YESNO | MB_ICONQUESTION);
    callback->Continue(r == IDYES, std::string());
    return true;
  }
  // prompt
  std::string value;
  bool ok = dialogs::promptDialog(owner, "Web98", message,
                                  str(default_prompt_text), &value);
  callback->Continue(ok, value);
  return true;
}

bool Web98Client::OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser,
                                       const CefString& message_text,
                                       bool is_reload,
                                       CefRefPtr<CefJSDialogCallback> callback) {
  HWND owner = reinterpret_cast<HWND>(browser->GetHost()->GetWindowHandle());
  std::string message = str(message_text);
  if (message.empty()) {
    message = is_reload ? "Do you want to reload this page?"
                        : "Changes you made may not be saved.";
  }
  int r = dialogs::messageBox(owner, message, "Web98",
                              MB_YESNO | MB_ICONWARNING);
  callback->Continue(r == IDYES, std::string());
  return true;
}

/* ------------------------------- keyboard --------------------------------- */

bool Web98Client::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                const CefKeyEvent& event,
                                CefEventHandle os_event,
                                bool* is_keyboard_shortcut) {
  if (event.type != KEYEVENT_RAWKEYDOWN) return false;
  if (ctx_.role == BrowserCtx::kDialog && !ctx_.dialog) return false;
  bool ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
  bool alt = (event.modifiers & EVENTFLAG_ALT_DOWN) != 0;
  bool shift = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  MainWindow* win = ctx_.window;
  if (!win && ctx_.tabs) win = ctx_.tabs->window();
  if (!win) return false;
  CommandCtx cmd;
  cmd.win = win;
  cmd.tabs = win->tabs();
  return handleAccelerator(event.windows_key_code, ctrl, alt, shift, cmd);
}

/* ----------------------------- life span ---------------------------------- */

void Web98Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (ctx_.role == BrowserCtx::kChrome && ctx_.window) {
    ctx_.window->attachChrome(browser);
    return;
  }
  if (ctx_.role == BrowserCtx::kDialog && ctx_.dialog) {
    ctx_.dialog->attachBrowser(browser);
    return;
  }
  if (ctx_.tabs && ctx_.tab) {
    ctx_.tabs->OnTabCreated(ctx_.tab, browser);
  } else if (detached_) {
    // The tab was closed while CreateBrowser was in flight; close the orphan.
    if (browser->GetHost()) browser->GetHost()->CloseBrowser(true);
  }
}

void Web98Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  browser_ = nullptr;
  if (ctx_.tabs && ctx_.tab) ctx_.tabs->OnTabClosed(ctx_.tab);
}

bool Web98Client::DoClose(CefRefPtr<CefBrowser> browser) {
  // Our browsers are all child windows parented to an app-provided top-level
  // frame (main window or dialog). By default CEF sends WM_CLOSE to the
  // browser's top-level parent when DoClose returns false, and on Windows
  // WM_CLOSE bubbles up the child hierarchy until it reaches a non-child
  // window -- which would destroy the whole frame. Return true so CEF does
  // NOT propagate the close to our parent window; we tear down the browser
  // child ourselves (via CloseBrowser in TabManager/MainWindow).
  return true;
}

bool Web98Client::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame, int popup_id,
                                const CefString& target_url,
                                const CefString& target_frame_name,
                                   cef_window_open_disposition_t target_disposition,
                                bool user_gesture,
                                const CefPopupFeatures& popupFeatures,
                                CefWindowInfo& windowInfo,
                                CefRefPtr<CefClient>& client,
                                CefBrowserSettings& settings,
                                CefRefPtr<CefDictionaryValue>& extra_info,
                                bool* no_javascript_access) {
  if (no_javascript_access) *no_javascript_access = true;
  std::string url = str(target_url);
  if (isSafeExternalUrl(url)) {
    dialogs::openExternalUrl(url);
    return true;
  }
  if (ctx_.tabs) {
    // Chrome/dialog pages are local-only: block any attempt to open a
    // web URL from them via window.open.
    if (ctx_.role != BrowserCtx::kTab) return true;
    std::string current = ctx_.tab ? ctx_.tab->url : std::string();
    // Popup blocker: script-initiated window.open (no user gesture) is
    // blocked unless the setting is off. User clicks always open in a tab.
    bool blockerOn = AppCore::get().settingsModel().getBool("popupBlocker", true);
    if (blockerOn && !user_gesture && !url.empty()) {
      AppCore::get().notifyPopupBlocked();
      return true;
    }
    if (isLoadableUrl(url) && isAllowedNavigation(url, isWebUrl(current))) {
      ctx_.tabs->create(url, true);
    }
  }
  return true;  // never create a CEF popup window
}

/* -------------------------------- loading --------------------------------- */

void Web98Client::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                       bool isLoading, bool canGoBack,
                                       bool canGoForward) {
  if (detached_) {
    return;
  }
  if (ctx_.role != BrowserCtx::kTab || !ctx_.tab || !ctx_.tabs) return;
  ctx_.tabs->OnLoadingStateChange(ctx_.tab, isLoading, canGoBack, canGoForward);
}

void Web98Client::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame, int httpStatusCode) {
  if (!frame->IsMain()) return;
  if (ctx_.role == BrowserCtx::kTab && ctx_.tab && ctx_.tabs) {
    ctx_.tabs->OnLoadEnd(ctx_.tab);
    return;
  }
  if (ctx_.role == BrowserCtx::kChrome && ctx_.window) {
    ctx_.window->onChromeLoaded();
    return;
  }
  if (ctx_.role == BrowserCtx::kDialog && ctx_.dialog) {
    ctx_.dialog->onLoaded();
  }
}

void Web98Client::OnLoadError(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                              const CefString& errorText,
                              const CefString& failedUrl) {
  if (detached_) {
    return;
  }
  if (ctx_.role != BrowserCtx::kTab || !frame->IsMain()) return;
  if (!ctx_.tab || !ctx_.tabs) return;
  if (errorCode == ERR_ABORTED) return;
  if (ctx_.tab->errorPageShown) return;
  ctx_.tabs->OnLoadError(ctx_.tab, static_cast<int>(errorCode), str(errorText),
                         str(failedUrl));
}

/* ------------------------------ permissions -------------------------------- */

bool Web98Client::OnRequestMediaAccessPermission(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    const CefString& requesting_origin, uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  std::string origin = originOf(str(requesting_origin));
  uint32_t mask = 0;
  if (requested_permissions &
      (CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE |
       CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE)) {
    mask |= CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE |
            CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;
  }
  if (requested_permissions & CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE)
    mask |= CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE;
  if (requested_permissions & CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE)
    mask |= CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE;

  bool audio = (requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) != 0;
  bool video = (requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) != 0;
  bool desktop = (requested_permissions & (CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE |
                                           CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE)) != 0;

  bool allow = AppCore::get().permissionAllowed(origin, "media") &&
               (!desktop || AppCore::get().permissionAllowed(origin, "display-capture"));
  if (!allow) {
    callback->Cancel();
    return true;
  }
  uint32_t granted = 0;
  if (audio) granted |= CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE;
  if (video) granted |= CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;
  callback->Continue(granted);
  return true;
}

bool Web98Client::OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser,
                                         uint64_t prompt_id,
                                         const CefString& requesting_origin,
                                         uint32_t requested_permissions,
                                         CefRefPtr<CefPermissionPromptCallback> callback) {
  std::string origin = originOf(str(requesting_origin));
  auto allowed = [&](uint32_t bit, const char* name) {
    if (!(requested_permissions & bit)) return true;
    return AppCore::get().permissionAllowed(origin, name);
  };
  bool ok = allowed(CEF_PERMISSION_TYPE_CAMERA_STREAM, "media") &&
             allowed(CEF_PERMISSION_TYPE_MIC_STREAM, "media") &&
             allowed(CEF_PERMISSION_TYPE_GEOLOCATION, "geolocation") &&
             allowed(CEF_PERMISSION_TYPE_NOTIFICATIONS, "notifications") &&
             allowed(CEF_PERMISSION_TYPE_POINTER_LOCK, "pointerLock") &&
             allowed(CEF_PERMISSION_TYPE_MIDI_SYSEX, "midiSysex") &&
             allowed(CEF_PERMISSION_TYPE_CLIPBOARD, "clipboard-read") &&
             allowed(CEF_PERMISSION_TYPE_KEYBOARD_LOCK, "keyboardLock");
  callback->Continue(ok ? CEF_PERMISSION_RESULT_ACCEPT
                        : CEF_PERMISSION_RESULT_DENY);
  return true;
}

/* ---------------------------- navigation policy ---------------------------- */

bool Web98Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request, bool user_gesture,
                                 bool is_redirect) {
  if (!frame->IsMain()) return false;
  std::string url = str(request->GetURL());

  if (ctx_.role == BrowserCtx::kTab) {
    Tab* tab = ctx_.tab;
    if (!tab) return false;
    if (isSafeExternalUrl(url)) {
      dialogs::openExternalUrl(url);
      return true;
    }
    // Programmatic loads and history navigations bypass the policy.
    if (!tab->expectedNav.empty() && urlsMatch(tab->expectedNav, url)) {
      tab->expectedNav.clear();
      return false;
    }
    uint32_t transition = static_cast<uint32_t>(request->GetTransitionType());
    if (transition & TT_FORWARD_BACK_FLAG) return false;
    return !isAllowedNavigation(url, isWebUrl(tab->url));
  }

  if (!ctx_.window && !ctx_.dialog) return false;  // DevTools/unowned client
  bool first = firstLocalNav_;
  firstLocalNav_ = false;
  if (first || urlsMatch(initialUrl_, url)) return false;
  return true;  // chrome/dialog pages are local-only
}

bool Web98Client::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   const CefString& target_url,
                                cef_window_open_disposition_t target_disposition,
                                   bool user_gesture) {
  if (ctx_.role != BrowserCtx::kTab) return true;
  std::string url = str(target_url);
  if (isSafeExternalUrl(url)) {
    dialogs::openExternalUrl(url);
    return true;
  }
  if (ctx_.tabs) {
    std::string current = ctx_.tab ? ctx_.tab->url : std::string();
    if (isLoadableUrl(url) && isAllowedNavigation(url, isWebUrl(current))) {
      ctx_.tabs->create(url, true);
    }
  }
  return true;
}

bool Web98Client::OnCertificateError(CefRefPtr<CefBrowser> browser,
                                     cef_errorcode_t cert_error,
                                     const CefString& request_url,
                                     CefRefPtr<CefSSLInfo> ssl_info,
                                     CefRefPtr<CefCallback> callback) {
  if (ctx_.role != BrowserCtx::kTab) {
    callback->Cancel();
    return true;
  }
  std::string key =
      std::to_string(browser->GetIdentifier()) + "|" + str(request_url);
  if (AppCore::get().certs().allows(key)) {
    callback->Continue();
    return true;
  }
  callback->Cancel();  // -> OnLoadError -> error page with "continue anyway"
  return true;
}

void Web98Client::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                            TerminationStatus status,
                                            int error_code,
                                            const CefString& error_string) {
  (void)error_code;
  (void)error_string;
  if (ctx_.role != BrowserCtx::kTab || !ctx_.tab || !ctx_.tabs) return;
  Tab* tab = ctx_.tab;
  if (!tab->crashedOnce) {
    tab->crashedOnce = true;
    if (browser->GetMainFrame()) {
      tab->expectedNav = tab->url;
      browser->GetMainFrame()->LoadURL(tab->url);
    }
    return;
  }
  ctx_.tabs->showError(tab, -1, "The tab process crashed", tab->url);
}

}  // namespace web98
