#pragma once
// One Web98Client per CEF browser. The role decides which policies apply:
//   kChrome - the retro chrome UI page (index.html, local-only)
//   kDialog - a dialog window page (dialogs/*.html, local-only)
//   kTab    - web content (full navigation/permission/download policy)
#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_dialog_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_focus_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"

#include "json.h"

namespace web98 {

class MainWindow;
class DialogWindow;
class TabManager;
struct Tab;

// Routing information attached to every browser.
struct BrowserCtx {
  enum Role { kChrome, kDialog, kTab };
  Role role = kChrome;
  MainWindow* window = nullptr;    // chrome + tab browsers
  DialogWindow* dialog = nullptr;  // dialog browsers
  TabManager* tabs = nullptr;      // owning tab manager (chrome + tab)
  Tab* tab = nullptr;              // the tab record (tab browsers only)
};

// Browser -> renderer IPC helpers (JSON travels as a string payload).
void SendIpcReply(CefRefPtr<CefFrame> frame, int64_t id, bool ok,
                  const Json& result);
void SendIpcEvent(CefRefPtr<CefFrame> frame, const std::string& channel,
                  const Json& payload);

// Thread-safe cache of the View > Encoding (Accept-Charset) value. Written
// from the UI thread when the setting changes; read from the IO thread in
// OnBeforeResourceLoad.
void setCharsetCache(const std::string& enc);
std::string getCharsetCache();

class Web98Client : public CefClient,
                    public CefContextMenuHandler,
                    public CefDialogHandler,
                    public CefDisplayHandler,
                    public CefDownloadHandler,
                    public CefFindHandler,
                    public CefFocusHandler,
                    public CefJSDialogHandler,
                    public CefKeyboardHandler,
                    public CefLifeSpanHandler,
                    public CefLoadHandler,
                    public CefPermissionHandler,
                    public CefRequestHandler,
                    public CefResourceRequestHandler {
 public:
  Web98Client(const BrowserCtx& ctx, const std::string& initialUrl);
  ~Web98Client() override;

  // Must run before the browser is destroyed: severs owner back-pointers so
  // late CEF callbacks never touch a freed tab/window.
  void Detach();

  const BrowserCtx& ctx() const { return ctx_; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }
  void SetBrowser(CefRefPtr<CefBrowser> browser) { browser_ = browser; }

  // Deliver an event to this browser's main frame (chrome/dialog/tab page).
  void SendEvent(const std::string& channel, const Json& payload);

  // --- CefClient -----------------------------------------------------------
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // --- CefContextMenuHandler ------------------------------------------------
  bool RunContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefContextMenuParams> params,
                      CefRefPtr<CefMenuModel> model,
                      CefRefPtr<CefRunContextMenuCallback> callback) override;

  // --- CefDialogHandler -----------------------------------------------------
  bool OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode,
                    const CefString& title,
                    const CefString& default_file_path,
                    const std::vector<CefString>& accept_filters,
                    const std::vector<CefString>& accept_extensions,
                    const std::vector<CefString>& accept_descriptions,
                    CefRefPtr<CefFileDialogCallback> callback) override;

  // --- CefDisplayHandler ----------------------------------------------------
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                              bool fullscreen) override;
  void OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                               double progress) override;
  void OnStatusMessage(CefRefPtr<CefBrowser> browser,
                       const CefString& value) override;

  // --- CefDownloadHandler ---------------------------------------------------
  bool CanDownload(CefRefPtr<CefBrowser> browser, const CefString& url,
                   const CefString& request_method) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

  // --- CefFindHandler -------------------------------------------------------
  void OnFindResult(CefRefPtr<CefBrowser> browser, int identifier,
                    int count, const CefRect& selection_rect, int active_match,
                    bool final_update) override;

  // --- CefFocusHandler ------------------------------------------------------
  bool OnSetFocus(CefRefPtr<CefBrowser> browser, FocusSource source) override;

  // --- CefJSDialogHandler ---------------------------------------------------
  bool OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& origin_url,
                  JSDialogType dialog_type, const CefString& message_text,
                  const CefString& default_prompt_text,
                  CefRefPtr<CefJSDialogCallback> callback,
                  bool& suppress_message) override;
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser,
                            const CefString& message_text, bool is_reload,
                            CefRefPtr<CefJSDialogCallback> callback) override;

  // --- CefKeyboardHandler ---------------------------------------------------
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  // --- CefLifeSpanHandler ---------------------------------------------------
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     int popup_id, const CefString& target_url,
                     const CefString& target_frame_name,
                     cef_window_open_disposition_t target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popupFeatures,
                     CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;

  // --- CefLoadHandler -------------------------------------------------------
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading,
                            bool canGoBack, bool canGoForward) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode, const CefString& errorText,
                   const CefString& failedUrl) override;

  // --- CefPermissionHandler -------------------------------------------------
  bool OnRequestMediaAccessPermission(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString& requesting_origin, uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback) override;
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser,
                              uint64_t prompt_id,
                              const CefString& requesting_origin,
                              uint32_t requested_permissions,
                              CefRefPtr<CefPermissionPromptCallback> callback) override;

  // --- CefRequestHandler ----------------------------------------------------
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture,
                      bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        cef_window_open_disposition_t target_disposition,
                        bool user_gesture) override;
  bool OnCertificateError(CefRefPtr<CefBrowser> browser,
                          cef_errorcode_t cert_error,
                          const CefString& request_url,
                          CefRefPtr<CefSSLInfo> ssl_info,
                          CefRefPtr<CefCallback> callback) override;
  bool GetAuthCredentials(CefRefPtr<CefBrowser> browser,
                          const CefString& origin_url, bool isProxy,
                          const CefString& host, int port,
                          const CefString& realm, const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request, bool is_navigation, bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;
  // --- CefResourceRequestHandler ---------------------------------------------
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status, int error_code,
                                 const CefString& error_string) override;

 private:
  IMPLEMENT_REFCOUNTING(Web98Client);
  DISALLOW_COPY_AND_ASSIGN(Web98Client);

  BrowserCtx ctx_;
  CefRefPtr<CefBrowser> browser_;
  std::string initialUrl_;
  bool firstLocalNav_ = true;
  bool closed_ = false;
  bool detached_ = false;
};

}  // namespace web98
