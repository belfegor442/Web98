#pragma once
// Abstraction over the rendering engine so alternative runtimes can be
// evaluated later. The CEF implementation lives in cef_engine.h.
#include <cstdint>
#include <string>

namespace web98 {

// UI-side callbacks. All invoked on the browser (UI) thread.
class EngineListener {
 public:
  virtual ~EngineListener() = default;

  virtual void OnTabCreated(uint64_t key, int browser_id) { (void)key; (void)browser_id; }
  virtual void OnTabClosed(uint64_t key) { (void)key; }
  virtual void OnAddressChanged(uint64_t key, const std::string& url) {
    (void)key;
    (void)url;
  }
  virtual void OnTitleChanged(uint64_t key, const std::string& title) {
    (void)key;
    (void)title;
  }
  virtual void OnLoadingStateChanged(uint64_t key, bool loading, bool can_back,
                                     bool can_forward) {
    (void)key;
    (void)loading;
    (void)can_back;
    (void)can_forward;
  }
  virtual void OnLoadEnd(uint64_t key, const std::string& url, int http_status) {
    (void)key;
    (void)url;
    (void)http_status;
  }
  // Main-frame load failures that show the Web98 error page.
  virtual void OnLoadError(uint64_t key, int code, const std::string& text,
                           const std::string& failed_url) {
    (void)key;
    (void)code;
    (void)text;
    (void)failed_url;
  }
  virtual void OnConsoleMessage(uint64_t key, const std::string& message, int level) {
    (void)key;
    (void)message;
    (void)level;
  }
  virtual void OnFindResult(uint64_t key, int count, bool finished) {
    (void)key;
    (void)count;
    (void)finished;
  }
  // mailto:/tel: (or other safe external) should be handed to the OS.
  virtual void OnExternalProtocol(const std::string& url) { (void)url; }
  // Page asked for a new window: return handled by engine policy directly.
};

class Web98Engine {
 public:
  virtual ~Web98Engine() = default;

  // Create a browser view parented to `parent_hwnd` (child bounds set later
  // via SetBounds). `key` is the UI-assigned tab identity.
  virtual bool CreateTab(uint64_t key, void* parent_hwnd, const std::string& url) = 0;
  virtual void CloseTab(uint64_t key, bool force) = 0;
  virtual void Load(uint64_t key, const std::string& url) = 0;
  virtual void Back(uint64_t key) = 0;
  virtual void Forward(uint64_t key) = 0;
  virtual void Reload(uint64_t key) = 0;
  virtual void Stop(uint64_t key) = 0;

  virtual bool Valid(uint64_t key) = 0;
  virtual bool CanGoBack(uint64_t key) = 0;
  virtual bool CanGoForward(uint64_t key) = 0;
  virtual bool IsLoading(uint64_t key) = 0;
  virtual int BrowserId(uint64_t key) = 0;

  virtual void SetBounds(uint64_t key, int x, int y, int w, int h) = 0;
  virtual void Focus(uint64_t key) = 0;
  virtual void ExecuteJS(uint64_t key, const std::string& code) = 0;
  virtual void SetZoom(uint64_t key, double level) = 0;
  virtual void Find(uint64_t key, const std::string& text, bool forward, bool find_next) = 0;
  virtual void StopFinding(uint64_t key, bool clear_selection) = 0;
  virtual void Print(uint64_t key) = 0;
  // View-source for the active page (implemented via data:text/html bridge? no:
  // engine-specific; CEF uses ExecuteJS + string visitor).
  virtual void ViewSource(uint64_t key) = 0;
};

}  // namespace web98
