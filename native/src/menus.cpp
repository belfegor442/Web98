#include "menus.h"

#include <windows.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <set>

#include "include/cef_request_context.h"
#include "include/cef_urlrequest.h"

#include "app_core.h"
#include "base.h"
#include "cef_engine.h"
#include "cef_util.h"
#include "dialogs.h"
#include "safeurl.h"
#include "settings.h"
#include "urlbar.h"
#include "window.h"

namespace web98 {
namespace {

// CefFrame edit commands (menus.js edit:page + context-menu items).
void editOp(CefRefPtr<CefBrowser> b, const std::string& op) {
  if (!b || !b->GetMainFrame()) return;
  CefRefPtr<CefFrame> f = b->GetMainFrame();
  if (op == "undo") f->Undo();
  else if (op == "redo") f->Redo();
  else if (op == "cut") f->Cut();
  else if (op == "copy") f->Copy();
  else if (op == "paste") f->Paste();
  else if (op == "delete") f->Delete();
  else if (op == "select-all") f->SelectAll();
}

void printTab(CefRefPtr<CefBrowser> b) {
  if (b && b->GetHost()) b->GetHost()->Print();
}

// menus.js savePage: dialog then write the DOM as HTML, with subresources
// (images + stylesheets) inlined as data: URIs for offline viewing — parity
// with Electron savePage(path,'HTMLComplete'). Fetching is done natively via
// CefURLRequest (browser process, so no page-CORS limits) on the UI thread;
// web tabs expose no IPC bridge, so nothing is handed to page script.

// Extract the value of attribute `name` from an HTML tag fragment.
std::string tagAttr(const std::string& tag, const std::string& name) {
  std::regex re("\\b" + name + "\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))",
                std::regex::icase);
  std::smatch m;
  if (!std::regex_search(tag, m, re)) return "";
  if (m[1].matched) return m[1].str();
  if (m[2].matched) return m[2].str();
  return m[3].str();
}

// A single in-flight subresource fetch + the shared saver state it updates.
class PageSaver : public CefURLRequestClient {
 public:
  static void Run(CefRefPtr<CefBrowser> browser, std::string html,
                  std::string pageUrl, std::string path) {
    CefRefPtr<PageSaver> s = new PageSaver(std::move(html), std::move(pageUrl),
                                           std::move(path), browser);
    s->Start();
  }

  // --- CefURLRequestClient (UI thread; same as creator) --------------------
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    std::string url = request->GetRequest()->GetURL().ToString();
    bool ok = request->GetRequestStatus() == UR_SUCCESS && !currentBody_.empty() &&
              !currentTooBig_;
    if (ok) {
      std::string mime = "application/octet-stream";
      if (request->GetResponse()) {
        std::string ct = request->GetResponse()->GetMimeType().ToString();
        if (!ct.empty()) mime = ct;
      }
      dataUris_[url] =
          "data:" + mime + ";base64," +
          base64Encode(reinterpret_cast<const unsigned char*>(currentBody_.data()),
                       currentBody_.size());
    }
    currentBody_.clear();
    currentUrl_.clear();
    currentTooBig_ = false;
    FetchNext();
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest> request, int64_t c,
                        int64_t t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request, int64_t c,
                          int64_t t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data,
                      size_t len) override {
    // Cap each subresource so a hostile/broken server streaming a huge body
    // cannot OOM the browser during Save Page; oversized fetches are treated
    // as failures in OnRequestComplete and the original URL is kept.
    constexpr size_t kMaxSubresource = 32 << 20;  // 32 MB
    if (currentBody_.size() + len > kMaxSubresource) {
      currentBody_.clear();
      currentTooBig_ = true;
      return;
    }
    currentBody_.append(static_cast<const char*>(data), len);
  }
  bool GetAuthCredentials(bool isProxy, const CefString& host, int port,
                          const CefString& realm, const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;  // no credentials for subresource fetches
  }

 private:
  PageSaver(std::string html, std::string pageUrl, std::string path,
            CefRefPtr<CefBrowser> browser)
      : html_(std::move(html)),
        pageUrl_(std::move(pageUrl)),
        path_(std::move(path)) {
    if (browser && browser->GetHost())
      ctx_ = browser->GetHost()->GetRequestContext();
  }

  void Start() {
    // Collect <img src> and <link rel=stylesheet href> targets.
    std::set<std::string> uniq;
    std::regex tagRe("<(?:img|link)\\b[^>]*>", std::regex::icase);
    for (auto it = std::sregex_iterator(html_.begin(), html_.end(), tagRe);
         it != std::sregex_iterator(); ++it) {
      std::string tag = it->str();
      bool isLink = tag.size() > 5 && (tag[1] == 'l' || tag[1] == 'L');
      std::string attr = isLink ? "href" : "src";
      std::string raw = tagAttr(tag, attr);
      if (isLink) {
        std::string rel = tagAttr(tag, "rel");
        std::string relL = asciiLower(rel);
        if (relL.find("stylesheet") == std::string::npos) continue;
      }
      std::string abs = resolveUrl(pageUrl_, raw);
      if (!abs.empty()) uniq.insert(abs);
    }
    urls_.assign(uniq.begin(), uniq.end());
    if (urls_.empty()) {
      Finish();
      return;
    }
    FetchNext();
  }

  void FetchNext() {
    if (index_ >= urls_.size()) {
      Finish();
      return;
    }
    currentUrl_ = urls_[index_++];
    CefRefPtr<CefRequest> req = CefRequest::Create();
    req->SetURL(currentUrl_);
    req->SetMethod("GET");
    CefURLRequest::Create(req, this, ctx_);  // kept alive by CEF until complete
  }

  void Finish() {
    // Replace each resolved URL with its data: URI (longest first so nested
    // prefixes never clobber one another).
    std::sort(urls_.begin(), urls_.end(),
              [](const std::string& a, const std::string& b) {
                return a.size() > b.size();
              });
    for (const auto& u : urls_) {
      auto found = dataUris_.find(u);
      if (found == dataUris_.end()) continue;  // fetch failed -> keep original
      size_t pos = 0;
      while ((pos = html_.find(u, pos)) != std::string::npos) {
        html_.replace(pos, u.size(), found->second);
        pos += found->second.size();
      }
    }
    writeFileAtomic(path_, html_);
  }

  std::string html_, pageUrl_, path_;
  CefRefPtr<CefRequestContext> ctx_;
  std::vector<std::string> urls_;
  std::map<std::string, std::string> dataUris_;
  size_t index_ = 0;
  std::string currentUrl_, currentBody_;
  bool currentTooBig_ = false;
  IMPLEMENT_REFCOUNTING(PageSaver);
  DISALLOW_COPY_AND_ASSIGN(PageSaver);
};

// Fetch a single image and put its decoded pixels on the clipboard (CF_DIB)
// so "Copy Image" copies the image itself rather than its URL. Kept alive by
// CEF until OnRequestComplete, same as PageSaver.
class CopyImageClient : public CefURLRequestClient {
 public:
  static void Run(CefRefPtr<CefBrowser> browser, const std::string& url) {
    CefRefPtr<CopyImageClient> c = new CopyImageClient(browser);
    c->Start(url);
  }

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    bool ok = request->GetRequestStatus() == UR_SUCCESS && !body_.empty() &&
              !tooBig_;
    if (ok) dialogs::setClipboardImage(body_.data(), body_.size());
    else dialogs::setClipboardText(lastUrl_);  // fallback (also on oversize)
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest> r, int64_t c, int64_t t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> r, int64_t c, int64_t t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> r, const void* data, size_t len) override {
    // Same cap as PageSaver: a huge "image" body must not OOM Copy Image.
    constexpr size_t kMaxImage = 64 << 20;  // 64 MB
    if (body_.size() + len > kMaxImage) {
      body_.clear();
      tooBig_ = true;
      return;
    }
    body_.append(static_cast<const char*>(data), len);
  }
  bool GetAuthCredentials(bool isProxy, const CefString& host, int port,
                          const CefString& realm, const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;
  }

 private:
  explicit CopyImageClient(CefRefPtr<CefBrowser> browser) {
    if (browser && browser->GetHost())
      ctx_ = browser->GetHost()->GetRequestContext();
  }
  void Start(const std::string& url) {
    lastUrl_ = url;
    CefRefPtr<CefRequest> req = CefRequest::Create();
    req->SetURL(url);
    req->SetMethod("GET");
    CefURLRequest::Create(req, this, ctx_);
  }

  CefRefPtr<CefRequestContext> ctx_;
  std::string body_, lastUrl_;
  bool tooBig_ = false;
  IMPLEMENT_REFCOUNTING(CopyImageClient);
  DISALLOW_COPY_AND_ASSIGN(CopyImageClient);
};

void savePageTab(Tab* tab) {
  if (!tab) return;
  std::string suggested = sanitizeText(tab->title.empty() ? "page" : tab->title, 80);
  for (char& c : suggested) {
    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|') {
      c = '_';
    }
  }
  suggested += ".html";
  HWND owner = tab->client && tab->client->browser()
                   ? reinterpret_cast<HWND>(tab->client->browser()->GetHost()
                                                ->GetWindowHandle())
                   : nullptr;
  std::string path = dialogs::saveFileDialog(owner, "Save Web Page", suggested,
                                             {"htm", "html"});
  if (path.empty()) return;
  CefRefPtr<CefBrowser> b = tab->client ? tab->client->browser() : nullptr;
  if (!b || !b->GetMainFrame()) return;
  std::string pageUrl = b->GetMainFrame()->GetURL().ToString();
  b->GetMainFrame()->GetSource(new StringSink(
      [b, pageUrl, path](const CefString& s) {
        PageSaver::Run(b, s.ToString(), pageUrl, path);
      }));
}

}  // namespace

// --- context menu -------------------------------------------------------------

Json contextTemplate(const Json& params, bool canBack, bool canForward,
                     bool loading) {
  Json items = Json::makeArray();
  auto pushItem = [&items](const std::string& id, const std::string& label,
                           bool enabled) {
    Json o = Json::makeObject();
    o.obj.emplace_back("id", Json::makeString(id));
    o.obj.emplace_back("label", Json::makeString(label));
    if (!enabled) o.obj.emplace_back("enabled", Json::makeBool(false));
    items.arr.push_back(std::move(o));
  };
  auto pushSep = [&items]() {
    Json o = Json::makeObject();
    o.obj.emplace_back("sep", Json::makeBool(true));
    items.arr.push_back(std::move(o));
  };

  const Json* link = params.find("linkURL");
  std::string linkURL = link ? link->stringOr("") : "";
  const Json* sel = params.find("selectionText");
  std::string selectionText = sel ? sel->stringOr("") : "";
  bool hasSel = !selectionText.empty();
  const Json* ed = params.find("isEditable");
  bool isEditable = ed && ed->boolOr(false);
  const Json* mt = params.find("mediaType");
  std::string mediaType = mt ? mt->stringOr("none") : "none";
  const Json* src = params.find("srcURL");
  std::string srcURL = src ? src->stringOr("") : "";

  pushItem("back", "Back", canBack);
  pushItem("forward", "Forward", canForward);
  pushItem("reload", "Reload", true);
  pushItem("stop", "Stop", loading);
  pushSep();

  if (!linkURL.empty()) {
    pushItem("open-link-tab", "Open Link in New Tab", true);
    pushItem("copy-link", "Copy Link Address", true);
    pushItem("save-link", "Save Link As...", true);
    pushSep();
  }
  if (isEditable) {
    pushItem("cut", "Cut", true);
    pushItem("copy", "Copy", true);
    pushItem("paste", "Paste", true);
    pushItem("delete", "Delete", true);
    pushSep();
  } else if (hasSel) {
    pushItem("copy", "Copy", true);
    pushSep();
  }
  if (mediaType == "image" || !srcURL.empty()) {
    pushItem("save-image", "Save Image As...", true);
    pushItem("copy-image", "Copy Image", true);
    pushSep();
  }
  pushItem("select-all", "Select All", true);
  pushSep();
  pushItem("view-source", "View Source", true);
  pushItem("save-page", "Save Page As...", true);
  pushItem("print", "Print...", true);
  pushSep();
  pushItem("inspect", "Inspect Element", true);
  return items;
}

// --- commands -----------------------------------------------------------------

bool runCommand(const std::string& name, const Json& arg, const CommandCtx& ctx) {
  AppCore& app = AppCore::get();
  MainWindow* win = ctx.win;
  TabManager* tabs = ctx.tabs;
  Tab* tab = tabs ? tabs->active() : nullptr;
  bool argIsString = arg.isString();
  bool argIsNumber = arg.isNumber();
  std::string argS = argIsString ? arg.str : std::string();
  double argN = argIsNumber ? arg.num : 1.0;

  auto emit = [&](const std::string& channel) {
    if (win) win->emit(channel, Json::makeNull());
  };

  if (name == "file:new-tab") {
    if (!tabs) return true;
    tabs->create(argIsString && isLoadableUrl(argS) ? argS : std::string(), true);
    return true;
  }
  if (name == "file:new-window") {
    app.createMainWindow(true);
    return true;
  }
  if (name == "file:close-tab") {
    if (tabs && tab) tabs->close(tab->id);
    return true;
  }
  if (name == "file:close-window") {
    if (win) win->requestClose();
    return true;
  }
  if (name == "tab:duplicate") {
    if (tabs && tab) tabs->duplicate(tab->id);
    return true;
  }
  if (name == "tab:pin") {
    if (tabs && tab) tabs->togglePin(tab->id);
    return true;
  }
  if (name == "tab:reopen") {
    if (tabs) tabs->reopenClosed();
    return true;
  }
  if (name == "file:open-location") {
    emit("chrome:focus-address");
    return true;
  }
  if (name == "file:open") {
    HWND owner = win ? win->hwnd() : nullptr;
    std::string path = dialogs::openFileDialog(
        owner, "Open File",
        {"htm", "html", "txt", "log", "xml", "css", "js", "json", "md"});
    if (!path.empty()) {
      std::string url = pathToFileUrl(path);
      if (tabs) tabs->create(url, true);
    }
    return true;
  }
  if (name == "file:save-page") {
    savePageTab(tab);
    return true;
  }
  if (name == "file:print") {
    if (tab && tab->client) printTab(tab->client->browser());
    return true;
  }
  if (name == "file:print-pdf") {
    // Save as PDF: pick a destination, then PrintToPDF.
    if (tab && tab->client && tab->client->browser()) {
      HWND owner = ctx.win ? ctx.win->hwnd() : nullptr;
      std::string suggested = "page.pdf";
      if (!tab->url.empty()) {
        // Derive a filename from the URL path.
        size_t slash = tab->url.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < tab->url.size()) {
          suggested = tab->url.substr(slash + 1);
          size_t q = suggested.find_first_of("?#");
          if (q != std::string::npos) suggested = suggested.substr(0, q);
          if (suggested.empty()) suggested = "page";
          suggested += ".pdf";
        }
      }
      std::string path = dialogs::saveFileDialog(owner, "Save as PDF",
                                                 suggested, {"pdf"});
      if (!path.empty()) {
        if (path.size() < 4 || asciiLower(path.substr(path.size() - 4)) != ".pdf")
          path += ".pdf";
        CefPdfPrintSettings settings;
        settings.display_header_footer = false;
        tab->client->browser()->GetHost()->PrintToPDF(path, settings, nullptr);
      }
    }
    return true;
  }
  if (name == "file:exit") {
    app.quit();
    return true;
  }

  if (name == "edit:page") {
    if (!tab || !tab->client) return true;
    editOp(tab->client->browser(), argS);
    return true;
  }

  if (name == "view:refresh") {
    if (tab && tab->client && tab->client->browser())
      tab->client->browser()->Reload();
    return true;
  }
  if (name == "view:stop") {
    if (tab && tab->loading && tab->client && tab->client->browser())
      tab->client->browser()->StopLoad();
    return true;
  }
  if (name == "view:home") {
    if (tabs) tabs->load(tab, app.settingsModel().getString("homepage", homepageUrl()));
    return true;
  }
  if (name == "view:fullscreen") {
    if (win) win->setFullscreen(!win->isFullscreen());
    return true;
  }
  if (name == "view:zoom") {
    double z = std::min(3.0, std::max(0.25, argN));
    if (tabs) tabs->setZoom(z);
    app.setSetting("zoom", Json::makeNumber(z));
    // Also remember per-site.
    if (tab && !tab->url.empty()) {
      std::string origin = originOf(tab->url);
      if (!origin.empty() && origin != "file://") app.setSiteZoom(origin, z);
    }
    return true;
  }
  if (name == "view:toolbar") {
    bool cur = app.settingsModel().getBool("toolbarVisible", true);
    app.setSetting("toolbarVisible", Json::makeBool(!cur));
    return true;
  }
  if (name == "view:statusbar") {
    bool cur = app.settingsModel().getBool("statusBar", true);
    app.setSetting("statusBar", Json::makeBool(!cur));
    return true;
  }
  if (name == "view:encoding") {
    // arg is the charset ("" = browser default). Persist it, refresh the
    // thread-safe cache (inside setSetting), then reload the active tab so
    // the new Accept-Charset takes effect on the next request.
    std::string enc = arg.isString() ? arg.str : std::string();
    app.setSetting("textEncoding", Json::makeString(enc));
    if (tab && tab->client && tab->client->browser() &&
        tab->client->browser()->GetMainFrame()) {
      tab->expectedNav = tab->url;
      tab->client->browser()->GetMainFrame()->LoadURL(tab->url);
    }
    return true;
  }
  if (name == "view:devtools") {
    if (tab && tab->client && tab->client->browser()) {
      CefWindowInfo wi;
      wi.SetAsPopup(nullptr, "DevTools");
      tab->client->browser()->GetHost()->ShowDevTools(wi, nullptr, CefBrowserSettings(), CefPoint());
    }
    return true;
  }
  if (name == "view:source") {
    viewSourceAsync(ctx);
    return true;
  }
  if (name == "view:page-info") {
    app.openDialog("pageinfo", win);
    return true;
  }

  if (name == "tools:downloads") {
    app.openDialog("downloads", win);
    return true;
  }
  if (name == "tools:history") {
    app.openDialog("history", win);
    return true;
  }
  if (name == "tools:passwords") {
    app.openDialog("passwords", win);
    return true;
  }
  if (name == "tools:settings") {
    app.openDialog("settings", win);
    return true;
  }
  if (name == "help:about") {
    app.openDialog("about", win);
    return true;
  }
  if (name == "favorites:organize") {
    app.openDialog("favorites", win);
    return true;
  }

  if (name == "nav:search") {
    // menus.js searchInNewTab: open the search base in a new tab.
    std::string tmpl =
        searchTemplateFor(app.settingsModel().all());
    std::string base = searchTemplateBase(tmpl);
    if (tabs) tabs->create(base, true);
    return true;
  }
  return false;
}

// --- accelerators --------------------------------------------------------------

bool handleAccelerator(int code, bool ctrl, bool alt, bool shift,
                       const CommandCtx& ctx) {
  AppCore& app = AppCore::get();
  MainWindow* win = ctx.win;
  TabManager* tabs = ctx.tabs;
  Tab* tab = tabs ? tabs->active() : nullptr;
  auto emit = [&](const std::string& channel) {
    if (win) win->emit(channel, Json::makeNull());
  };
  auto hostOf = [&]() -> CefRefPtr<CefBrowser> {
    if (tab && tab->client && tab->client->browser())
      return tab->client->browser();
    return nullptr;
  };
  auto lower = [](int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; };
  int k = lower(code);

  if (ctrl && !alt && !shift) {
    switch (k) {
      case 't':
        if (tabs) tabs->create(std::string(), true);
        return true;
      case 'w':
        if (tabs && tab) tabs->close(tab->id);
        return true;
      case 'n':
        app.createMainWindow(true);
        return true;
      case 'l':
        emit("chrome:focus-address");
        return true;
      case 'o': {
        HWND owner = win ? win->hwnd() : nullptr;
        std::string path = dialogs::openFileDialog(
            owner, "Open File",
            {"htm", "html", "txt", "log", "xml", "css", "js", "json", "md"});
        if (!path.empty() && tabs) tabs->create(pathToFileUrl(path), true);
        return true;
      }
      case 'r':
        if (auto h = hostOf()) h->ReloadIgnoreCache();
        return true;
      case 'd':
        emit("chrome:add-favorite");
        return true;
      case 'f':
        emit("chrome:find");
        return true;
      case 'h':
        app.openDialog("history", win);
        return true;
      case 'j':
        app.openDialog("downloads", win);
        return true;
      case 'p':
        if (auto h = hostOf()) h->GetHost()->Print();
        return true;
      case 's':
        savePageTab(tab);
        return true;
      case 'e':
      case 'k':
        // Ctrl+E / Ctrl+K: open the search base (same as the Search button).
        runCommand("nav:search", Json::makeNull(), ctx);
        return true;
      case VK_OEM_PLUS:
      case '=':
      case '+': {
        double z = std::min(3.0, (tabs ? tabs->active() : nullptr)
                                     ? (tab ? tab->zoom : 1.0) + 0.1
                                     : 1.1);
        if (tabs) tabs->setZoom(z);
        app.setSetting("zoom", Json::makeNumber(z));
        if (tab && !tab->url.empty()) {
          std::string origin = originOf(tab->url);
          if (!origin.empty() && origin != "file://") app.setSiteZoom(origin, z);
        }
        return true;
      }
      case VK_OEM_MINUS:
      case '-': {
        double base = tab ? tab->zoom : 1.0;
        double z = std::max(0.25, base - 0.1);
        if (tabs) tabs->setZoom(z);
        app.setSetting("zoom", Json::makeNumber(z));
        if (tab && !tab->url.empty()) {
          std::string origin = originOf(tab->url);
          if (!origin.empty() && origin != "file://") app.setSiteZoom(origin, z);
        }
        return true;
      }
      case '0':
        if (tabs) tabs->setZoom(1.0);
        app.setSetting("zoom", Json::makeNumber(1.0));
        if (tab && !tab->url.empty()) {
          std::string origin = originOf(tab->url);
          if (!origin.empty() && origin != "file://") app.setSiteZoom(origin, 1.0);
        }
        return true;
      default:
        return false;
    }
  }

  if (ctrl && shift && !alt && k == 't') {
    // Ctrl+Shift+T: reopen closed tab (falls back to new window).
    if (tabs && tabs->reopenClosed()) return true;
    app.createMainWindow(true);
    return true;
  }
  if (ctrl && shift && !alt && k == 'w') {
    if (win) win->requestClose();
    return true;
  }
  if (ctrl && shift && !alt && k == 'n') {
    app.createMainWindow(true);
    return true;
  }

  // Ctrl+Tab / Ctrl+Shift+Tab: cycle tabs.
  if (ctrl && !alt && code == VK_TAB) {
    if (tabs && tabs->count() > 0) {
      Tab* cur = tabs->active();
      if (cur) {
        int n = tabs->count();
        int idx = tabs->indexOf(cur->id);
        int next = shift ? (idx - 1 + n) % n : (idx + 1) % n;
        int targetId = 0, i = 0;
        tabs->each([&](Tab* t) {
          if (i++ == next) targetId = t->id;
        });
        if (targetId) tabs->setActive(targetId);
      }
    }
    return true;
  }
  // Ctrl+1..8: jump to tab N; Ctrl+9: last tab.
  if (ctrl && !alt && !shift && k >= '1' && k <= '9') {
    if (tabs && tabs->count() > 0) {
      int want = k - '0';
      int n = tabs->count();
      int target = (want == 9) ? n : std::min(want, n);
      int i = 1, targetId = 0;
      tabs->each([&](Tab* t) {
        if (i++ == target) targetId = t->id;
      });
      if (targetId) tabs->setActive(targetId);
    }
    return true;
  }
  // Ctrl+U: view source.
  if (ctrl && !alt && !shift && k == 'u') {
    viewSourceAsync(ctx);
    return true;
  }
  // Ctrl+Shift+Delete: clear browsing data (privacy pane).
  if (ctrl && shift && !alt && k == VK_DELETE) {
    Json payload = Json::makeObject();
    payload.obj.emplace_back("pane", Json::makeString("privacy"));
    app.openDialog("settings", win, &payload);
    return true;
  }
  // Ctrl+Shift+F: AutoFill the active page's form with personal info.
  if (ctrl && shift && !alt && k == 'f') {
    auto h = hostOf();
    if (!h || !h->GetMainFrame()) return true;
    Json af = app.settingsModel().get("autofill");
    if (!af.isObject() || af.obj.empty()) return true;
    std::string data = af.dump();
    std::string js =
        "(function(){var af=" + data + ";"
        "var map={"
        "firstName:['firstname','first','fname','givenname','forename'],"
        "lastName:['lastname','last','lname','surname','familyname'],"
        "company:['company','org','organization','business'],"
        "address1:['address','address1','street','addr','line1'],"
        "address2:['address2','addr2','line2','suite','apt'],"
        "city:['city','town'],"
        "state:['state','province','region'],"
        "zip:['zip','zipcode','postal','postcode'],"
        "country:['country'],"
        "phone:['phone','tel','telephone','mobile'],"
        "email:['email','mail'],"
        "name:['fullname','yourname','customername']"
        "};"
        "function val(k){return af[k]||'';}"
        "function match(h){"
        "h=(h||'').toLowerCase().replace(/[^a-z0-9]/g,'');"
        "if(!h)return null;"
        "for(var k in map){if(map[k].indexOf(h)!==-1)return k;}"
        "for(var k in map){var a=map[k];for(var i=0;i<a.length;i++){"
        "if(h.indexOf(a[i])!==-1)return k;}}"
        "return null;}"
        "var skip=['hidden','submit','button','checkbox','radio','file',"
        "'image','password','reset','range','color'];"
        "var els=document.querySelectorAll('input,textarea');"
        "var filled=0;"
        "for(var i=0;i<els.length;i++){"
        "var el=els[i];"
        "if(skip.indexOf(el.type)!==-1)continue;"
        "if(el.disabled||el.readOnly)continue;"
        "var hint=el.name||el.id||el.placeholder||el.getAttribute('autocomplete')||'';"
        "var k=match(hint);"
        "if(!k)continue;"
        "var v=(k==='name')?((val('firstName')+' '+val('lastName')).trim()):val(k);"
        "if(!v)continue;"
        "if(el.value&&el.value.trim())continue;"
        "el.value=v;"
        "el.dispatchEvent(new Event('input',{bubbles:true}));"
        "el.dispatchEvent(new Event('change',{bubbles:true}));"
        "filled++;}"
        "return filled;"
        "})()";
    h->GetMainFrame()->ExecuteJavaScript(js, h->GetMainFrame()->GetURL(), 0);
    return true;
  }
  // F12: developer tools.
  if (code == VK_F12) {
    if (tab && tab->client && tab->client->browser() &&
        tab->client->browser()->GetHost()) {
      CefWindowInfo wi;
      CefBrowserSettings bs;
      tab->client->browser()->GetHost()->ShowDevTools(wi, nullptr, bs,
                                                      CefPoint());
    }
    return true;
  }
  // F7: caret browsing (toggle a visible text caret in the page).
  if (code == VK_F7) {
    auto h = hostOf();
    if (h && h->GetMainFrame()) {
      const std::string js =
          "(function(){var on=!window.__web98Caret;window.__web98Caret=on;"
          "try{document.designMode=on?'on':'off';}catch(e){}"
          "if(on){var b=document.body;if(b){b.focus();}}"
          "return on;})()";
      h->GetMainFrame()->ExecuteJavaScript(js, h->GetMainFrame()->GetURL(), 0);
    }
    return true;
  }

  if (alt && !ctrl && code == VK_LEFT) {
    if (auto h = hostOf()) {
      if (h->CanGoBack()) h->GoBack();
    }
    return true;
  }
  if (alt && !ctrl && code == VK_RIGHT) {
    if (auto h = hostOf()) {
      if (h->CanGoForward()) h->GoForward();
    }
    return true;
  }
  if (alt && !ctrl && k == 'd') {
    emit("chrome:focus-address");
    return true;
  }
  if (alt && !ctrl && code == VK_HOME) {
    // Alt+Home (IE classic): go to the homepage.
    if (tabs) tabs->load(tab, app.settingsModel().getString("homepage", homepageUrl()));
    return true;
  }
  if (code == VK_F1) {
    // F1 = Help (About dialog).
    app.openDialog("about", win);
    return true;
  }
  if (code == VK_F5) {
    if (ctrl) {
      // Ctrl+F5 = hard reload (ignore cache).
      if (auto h = hostOf()) h->ReloadIgnoreCache();
    } else {
      if (auto h = hostOf()) h->Reload();
    }
    return true;
  }
  if (code == VK_F3) {
    // F3 = Find Next (same direction as last search).
    emit("chrome:find-next");
    return true;
  }
  if (ctrl && !shift && !alt && k == 'g') {
    emit("chrome:find-next");
    return true;
  }
  if (ctrl && shift && !alt && k == 'g') {
    emit("chrome:find-prev");
    return true;
  }
  if (code == VK_F11) {
    if (win) win->setFullscreen(!win->isFullscreen());
    return true;
  }
  if (code == VK_F6) {
    emit("chrome:focus-address");
    return true;
  }
  if (code == VK_ESCAPE && tab && tab->loading) {
    if (auto h = hostOf()) h->StopLoad();
    return true;
  }
  return false;
}

// --- context-menu commands -----------------------------------------------------

void runContextCommand(const std::string& id, const Json& params,
                       const CommandCtx& ctx) {
  AppCore& app = AppCore::get();
  TabManager* tabs = ctx.tabs;
  // Prefer the explicit target tab (right-clicked tab) over the active tab.
  Tab* tab = ctx.tab ? ctx.tab : (tabs ? tabs->active() : nullptr);
  if (!tab || !tab->client) return;
  CefRefPtr<CefBrowser> b = tab->client->browser();
  if (!b) return;
  CefRefPtr<CefBrowserHost> host = b->GetHost();

  auto strOf = [&](const char* key) -> std::string {
    const Json* v = params.find(key);
    return v ? v->stringOr("") : std::string();
  };

  if (id == "back") {
    if (b->CanGoBack()) b->GoBack();
    return;
  }
  if (id == "forward") {
    if (b->CanGoForward()) b->GoForward();
    return;
  }
  if (id == "reload") {
    b->Reload();
    return;
  }
  if (id == "stop") {
    b->StopLoad();
    return;
  }
  if (id == "open-link-tab") {
    std::string u = strOf("linkURL");
    if (tabs && isLoadableUrl(u)) tabs->create(u, true);
    return;
  }
  if (id == "copy-link") {
    dialogs::setClipboardText(strOf("linkURL"));
    return;
  }
  if (id == "save-link") {
    std::string u = strOf("linkURL");
    if (isLoadableUrl(u)) host->StartDownload(u);
    return;
  }
  if (id == "save-image") {
    std::string u = strOf("srcURL");
    if (isLoadableUrl(u)) host->StartDownload(u);
    return;
  }
  if (id == "copy-image") {
    // Fetch and place the actual image pixels on the clipboard (CF_DIB);
    // fall back to the URL text when it isn't a loadable resource.
    std::string u = strOf("srcURL");
    if (isLoadableUrl(u)) CopyImageClient::Run(b, u);
    else dialogs::setClipboardText(u);
    return;
  }
  if (id == "cut") { editOp(b, "cut"); return; }
  if (id == "copy") { editOp(b, "copy"); return; }
  if (id == "paste") { editOp(b, "paste"); return; }
  if (id == "delete") { editOp(b, "delete"); return; }
  if (id == "select-all") { editOp(b, "select-all"); return; }
  if (id == "view-source") {
    viewSourceAsync(ctx);
    return;
  }
  if (id == "save-page") {
    savePageTab(tab);
    return;
  }
  if (id == "print") {
    host->Print();
    return;
  }
  if (id == "inspect") {
    CefWindowInfo wi;
    wi.SetAsPopup(nullptr, "DevTools");
    host->ShowDevTools(wi, nullptr, CefBrowserSettings(), CefPoint());
    return;
  }
  (void)app;
}

// --- async helpers -------------------------------------------------------------

void viewSourceAsync(const CommandCtx& ctx) {
  Tab* tab = ctx.tabs ? ctx.tabs->active() : nullptr;
  if (!tab || !tab->client) return;
  CefRefPtr<CefBrowser> b = tab->client->browser();
  if (!b || !b->GetMainFrame()) return;
  std::string url = tab->url;
  MainWindow* win = ctx.win;
  std::weak_ptr<int> weak = win ? win->weakAlive() : std::weak_ptr<int>();
  b->GetMainFrame()->GetSource(new StringSink([weak, url](const CefString& s) {
    // Skip if the source window was destroyed while the source was loading.
    if (weak.expired()) return;
    Json payload = Json::makeObject();
    payload.obj.emplace_back("html", Json::makeString(s.ToString()));
    payload.obj.emplace_back("url", Json::makeString(url));
    AppCore::get().openDialog("source", nullptr, &payload);
  }));
}

void savePageAsync(const CommandCtx& ctx) {
  Tab* tab = ctx.tabs ? ctx.tabs->active() : nullptr;
  savePageTab(tab);
}

}  // namespace web98
