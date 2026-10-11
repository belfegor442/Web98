#pragma once
// Application singleton: IPC dispatcher (port of src/main/ipc.js), window
// registry, persistence (storage/settings/session), downloads, certificates
// and permissions. CEF is only required for the IPC/download call sites.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cef_client.h"
#include "downloads.h"
#include "json.h"
#include "security.h"
#include "settings.h"
#include "session.h"
#include "storage.h"

namespace web98 {

class MainWindow;
class DialogWindow;
struct Tab;

class AppCore : public DownloadListener {
 public:
  static AppCore& get();

  AppCore(const AppCore&) = delete;
  AppCore& operator=(const AppCore&) = delete;

  // DownloadListener: forward the coalesced list to every chrome renderer.
  void OnDownloadsChanged() override;

  // --- lifecycle ----------------------------------------------------------
  // Opens the profile DB, loads settings, starts downloads persistence.
  // Returns false when the profile cannot be opened.
  bool init();
  // Creates the initial window(s) from the stored session (or a fresh one).
  // Called from CefApp::OnContextInitialized.
  void createInitialWindows();
  // Persist the session for every open main window. `closing` mirrors
  // Electron's onWindowClosing: a window being closed is excluded when
  // other windows are still live (its own tabs are the session only when
  // it is the last one).
  void saveSession(MainWindow* closing = nullptr);
  // Called from WM_DESTROY: parks the object until the loop turns over.
  void retireMainWindow(MainWindow* w);
  // PostQuitMessage once no main window and no dialog is left.
  void onLastWindowClosed();
  // Final teardown before CefShutdown: closes every remaining window so the
  // unique_ptr destructors never run against a dead CEF context.
  void shutdown();
  int64_t startupAtMs() const { return startupAtMs_; }

  // --- services ------------------------------------------------------------
  Storage& storage() { return storage_; }
  SettingsModel& settingsModel() { return settings_; }
  Downloads& downloads() { return downloads_; }
  CertGrants& certs() { return certs_; }

  // Electron permissionDecision parity (settings.permissions store).
  bool permissionAllowed(const std::string& origin, const std::string& permission);
  void listGrants(Json& out) const;
  void setGrant(const std::string& origin, const std::string& permission,
                const Json& value);

  // Apply a settings patch from the renderer; returns the full settings map
  // (plus `rejected` keys when anything was refused).
  Json applySettingsPatch(const Json& patch);
  // Store a settings key coming from the app itself (download dir, zoom...).
  void setSetting(const std::string& key, const Json& value);

  // --- window registry ------------------------------------------------------
  MainWindow* createMainWindow(bool show);
  MainWindow* primaryWindow();
  // Open a dialogs/<page>.html window; returns false for unknown pages.
  // `payload` (optional) is delivered once the page finished loading.
  bool openDialog(const std::string& page, MainWindow* owner,
                  const Json* payload = nullptr);
  void closeAllMainWindows();
  // Electron wm.quit(): ask every window to close, then exit the loop.
  void quit();
  size_t windowCount() const { return mainWindows_.size(); }
  const std::vector<std::unique_ptr<MainWindow>>& mainWindows() const {
    return mainWindows_;
  }
  const std::vector<std::unique_ptr<DialogWindow>>& dialogs() const {
    return dialogs_;
  }
  void removeDialog(DialogWindow* d);
  void notifyQuit() { quitting_ = true; }
  bool quitting() const { return quitting_; }

  // --- events ---------------------------------------------------------------
  // Electron deps.broadcast: reaches the chrome renderer of every main
  // window. (Dialogs are not subscribed in Electron either; see report.)
  void broadcast(const std::string& channel, const Json& payload);

  // --- IPC ------------------------------------------------------------------
  // One dispatcher for every web98:ipc message. `isInvoke` selects the
  // reply/event semantics of preload.js (invoke -> Promise, send -> none).
  void handleIpc(const BrowserCtx& ctx, const std::string& channel,
                 const Json& arg, bool isInvoke, int64_t id,
                 CefRefPtr<CefFrame> frame);

  // --- downloads (CEF id mapping) ------------------------------------------
  // Called from OnBeforeDownload: registers the record + maps the CEF id.
  // Returns the assigned Web98 id (0 on failure) and the resolved save path.
  int registerCefDownload(uint32_t cefId, const std::string& url,
                          const std::string& suggestedName, std::string* outPath);
  // Called from OnDownloadUpdated: progress/state + cancel/pause hooks.
  void updateCefDownload(CefRefPtr<CefDownloadItem> item,
                         CefRefPtr<CefDownloadItemCallback> callback);
  // Handle a dangerous-extension confirmation (MessageBox) for `id`.
  void confirmDangerousDownload(int id);

 private:
  AppCore() = default;

  // Reply helper: ok=false when the handler threw (throwIpc semantics).
  void replyError(int64_t id, CefRefPtr<CefFrame> frame,
                  const std::string& message);

  Storage storage_;
  SettingsModel settings_;
  Downloads downloads_{std::string()};
  CertGrants certs_;
  Json permissions_ = Json::makeObject();  // {origin:{perm:bool}}

  std::vector<std::unique_ptr<MainWindow>> mainWindows_;
  // Destroyed windows parked until the next loop turn (WM_DESTROY runs
  // inside the object's own WndProc; unique_ptr erase there is unsafe).
  std::vector<std::unique_ptr<MainWindow>> retired_;
  std::vector<std::unique_ptr<DialogWindow>> dialogs_;
  std::vector<std::unique_ptr<DialogWindow>> retiredDialogs_;
  bool quitting_ = false;
  bool nextWindowShow_ = true;

  // CEF download id -> Web98 record id.
  std::map<uint32_t, int> cefDownloadIds_;

  // Boot timestamp for app:stats uptime.
  int64_t startupAtMs_ = 0;
};

}  // namespace web98
