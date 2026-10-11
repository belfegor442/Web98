#pragma once
// Web98 CefApp: browser-process bootstrap (creates the initial windows) and
// render-process bridge (injects window.web98 / web98home / web98cert into
// our local pages only, mirroring the Electron preload allowlists).
#include "include/cef_app.h"

namespace web98 {

class Web98App : public CefApp,
                 public CefBrowserProcessHandler,
                 public CefRenderProcessHandler {
 public:
  Web98App() = default;

  // --- CefApp ---------------------------------------------------------------
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;

  // --- CefBrowserProcessHandler ---------------------------------------------
  // Creates the initial window(s) from the stored session. Runs on the UI
  // thread during CefInitialize.
  void OnContextInitialized() override;

  // --- CefRenderProcessHandler ----------------------------------------------
  // Called before any page script of the main frame runs: installs the
  // native postMessage hook and evals the bridge shim (allowlists included).
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  // Routes browser->renderer replies/events into the shim.
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  IMPLEMENT_REFCOUNTING(Web98App);
  DISALLOW_COPY_AND_ASSIGN(Web98App);
};

}  // namespace web98
