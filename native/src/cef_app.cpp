#include "cef_app.h"

#include <windows.h>

#include <cstdio>
#include <string>

#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"
#include "include/cef_version_info.h"

#include "app_core.h"
#include "base.h"

namespace web98 {
namespace {

// Which local page owns this frame -> which bridge surface it may see.
// Mirrors the Electron split: chrome/dialogs get the full window.web98,
// tabs get nothing but the startpage (web98home) / error page (web98cert).
enum class BridgeKind { None, Chrome, Dialog, Startpage, Error };

bool urlIsFilePage(const std::string& url, const std::string& fileUrl) {
  if (url.size() < fileUrl.size()) return false;
  if (url.compare(0, fileUrl.size(), fileUrl) != 0) return false;
  if (url.size() == fileUrl.size()) return true;
  char next = url[fileUrl.size()];
  return next == '?' || next == '#';
}

BridgeKind classifyFrame(const std::string& url) {
  if (url.compare(0, 5, "file:") != 0) return BridgeKind::None;
  // Strip query and fragment so dialogs/history.html?q=1 still classifies.
  std::string clean = url;
  {
    size_t cut = clean.find_first_of("?#");
    if (cut != std::string::npos) clean.resize(cut);
  }
  const std::string res = resourceDir();
  if (urlIsFilePage(clean, pathToFileUrl(pathJoin(res, "index.html"))))
    return BridgeKind::Chrome;
  if (urlIsFilePage(clean, pathToFileUrl(pathJoin(res, "startpage.html"))))
    return BridgeKind::Startpage;
  if (urlIsFilePage(clean, pathToFileUrl(pathJoin(res, "error.html"))))
    return BridgeKind::Error;
  const std::string dialogBase =
      pathToFileUrl(pathJoin(pathJoin(res, "dialogs"), ""));
  if (clean.compare(0, dialogBase.size(), dialogBase) == 0 &&
      clean.find(".html", dialogBase.size()) == clean.size() - 5) {
    return BridgeKind::Dialog;
  }
  return BridgeKind::None;
}

// window.__web98_post(isInvoke, id, channel, payloadJson) -> browser.
class Web98BridgeHandler : public CefV8Handler {
 public:
  Web98BridgeHandler() = default;

  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    if (name != "__web98_post") return false;
    if (arguments.size() < 4) return true;  // swallow malformed calls
    const bool isInvoke = arguments[0]->GetIntValue() != 0;
    const double id = arguments[1]->GetDoubleValue();
    std::string channel = arguments[2]->GetStringValue().ToString();
    std::string payloadJson = arguments[3]->GetStringValue().ToString();
    // Hard caps; the JS shim already enforces the channel allowlists.
    if (channel.empty() || channel.size() > 64) return true;
    if (payloadJson.size() > (1u << 20)) return true;
    CefRefPtr<CefV8Context> ctx = CefV8Context::GetCurrentContext();
    if (!ctx || !ctx->GetFrame()) return true;
    CefRefPtr<CefProcessMessage> msg =
        CefProcessMessage::Create("web98:ipc");
    CefRefPtr<CefListValue> args = msg->GetArgumentList();
    args->SetBool(0, isInvoke);
    args->SetDouble(1, id);
    args->SetString(2, channel);
    args->SetString(3, payloadJson);
    ctx->GetFrame()->SendProcessMessage(PID_BROWSER, msg);
    retval = CefV8Value::CreateUndefined();
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(Web98BridgeHandler);
  DISALLOW_COPY_AND_ASSIGN(Web98BridgeHandler);
};

std::string versionsJson() {
  char buf[160];
  std::snprintf(
      buf, sizeof(buf),
      "{\"cef\":\"%d.%d.%d+g%d+chromium-%d.%d.%d.%d\","
      "\"chrome\":\"%d.%d.%d.%d\"}",
      cef_version_info(0), cef_version_info(1), cef_version_info(2),
      cef_version_info(3), cef_version_info(4), cef_version_info(5),
      cef_version_info(6), cef_version_info(7), cef_version_info(4),
      cef_version_info(5), cef_version_info(6), cef_version_info(7));
  return buf;
}

// Allowlists + dispatch shim; injected before any page script. `kind` picks
// the exposed surface, `versions` is the JSON reported by window.web98.
std::string buildShim(const char* kind) {
  return std::string(R"JS(
(function () {
  if (window.__web98_shim) return;
  window.__web98_shim = true;
  var pending = {};
  var nextId = 1;
  var listeners = {};
  function post(isInvoke, id, channel, payloadJson) {
    window.__web98_post(isInvoke ? 1 : 0, id, channel, payloadJson);
  }
  window.__web98_reply = function (id, ok, jsonStr) {
    var p = pending[id];
    if (!p) return;
    delete pending[id];
    var value = null;
    try { value = JSON.parse(jsonStr); } catch (e) { value = null; }
    if (ok) p.resolve(value);
    else p.reject(new Error((value && value.message) || 'IPC failed'));
  };
  window.__web98_event = function (channel, jsonStr) {
    var fns = listeners[channel];
    if (!fns) return;
    var payload = null;
    try { payload = JSON.parse(jsonStr); } catch (e) { payload = null; }
    for (var i = 0; i < fns.length; i++) {
      try { fns[i](payload); } catch (e) {}
    }
  };
  var INVOKE = [
    'app:command', 'app:find', 'app:open-url', 'app:stats', 'app:view-source',
    'bookmarks:add', 'bookmarks:export', 'bookmarks:import', 'bookmarks:list',
    'bookmarks:quick', 'bookmarks:remove', 'bookmarks:toggle-quick',
    'bookmarks:update',
    'chrome:bounds',
    'downloads:cancel', 'downloads:clearFinished', 'downloads:list',
    'downloads:open', 'downloads:pause', 'downloads:remove', 'downloads:resume',
    'downloads:showFolder',
    'history:clear', 'history:delete', 'history:list',
    'home:action',
    'nav:back', 'nav:forward', 'nav:go', 'nav:home', 'nav:reload', 'nav:stop',
    'omni:suggest',
    'pageinfo:load',
    'settings:clearData', 'settings:get', 'settings:grants',
    'settings:pickDownloadsDir', 'settings:revoke',
    'settings:set',
    'tabs:activate', 'tabs:close', 'tabs:create', 'tabs:list', 'tabs:move',
    'tabs:pin',
    'win:close', 'win:isMax', 'win:max', 'win:min', 'win:title',
    'window:close', 'window:open'
  ];
  var SEND = ['chrome:popup', 'ctx:command', 'win:drag'];
  var ON = [
    'app:ready',
    'bookmarks:changed',
    'chrome:add-favorite', 'chrome:find', 'chrome:find-next', 'chrome:find-prev',
    'chrome:find-result', 'chrome:focus-address',
    'chrome:popup-blocked',
    'chrome:status',
    'chrome:tab-focused',
    'ctx',
    'downloads',
    'history:changed',
    'nav',
    'pageinfo:cookies', 'pageinfo:data',
    'payload',
    'settings:changed',
    'tabs',
    'win:active', 'win:state'
  ];
  function makeApi() {
    return {
      versions: )JS") + versionsJson() + R"JS(,
      invoke: function (channel, data) {
        if (INVOKE.indexOf(channel) === -1) {
          return Promise.reject(new Error('Blocked IPC channel: ' + channel));
        }
        return new Promise(function (resolve, reject) {
          var id = nextId++;
          pending[id] = { resolve: resolve, reject: reject };
          post(true, id, channel,
               JSON.stringify(data === undefined ? null : data));
        });
      },
      send: function (channel, data) {
        if (SEND.indexOf(channel) === -1) return;
        post(false, 0, channel,
             JSON.stringify(data === undefined ? null : data));
      },
      on: function (channel, callback) {
        if (ON.indexOf(channel) === -1) return function () {};
        (listeners[channel] = listeners[channel] || []).push(callback);
        return function () {
          var fns = listeners[channel];
          if (!fns) return;
          var i = fns.indexOf(callback);
          if (i !== -1) fns.splice(i, 1);
        };
      }
    };
  }
  var kind = ")JS" + kind + R"JS(";
  if (kind === 'chrome' || kind === 'dialog') {
    window.web98 = makeApi();
  } else if (kind === 'startpage') {
    var api = makeApi();
    window.web98home = {
      action: function (name, arg) {
        return api.invoke('home:action', {
          name: String(name || ''),
          arg: arg == null ? '' : String(arg)
        });
      },
      quick: function () {
        return api.invoke('bookmarks:quick', null);
      },
      list: function () {
        return api.invoke('bookmarks:list', null);
      },
      history: function (limit) {
        return api.invoke('history:list', { limit: Number(limit) || 8 });
      },
      open: function (url) {
        return api.invoke('home:action', { name: 'search', arg: String(url || '') });
      },
      toggleQuick: function (id) {
        return api.invoke('bookmarks:toggle-quick', { id: Number(id) || 0 });
      },
      suggest: function (q) {
        return api.invoke('omni:suggest', { q: String(q || '') });
      },
      settings: function () {
        return api.invoke('settings:get', null);
      },
      setSetting: function (patch) {
        return api.invoke('settings:set',
          patch && typeof patch === 'object' ? patch : {});
      }
    };
  } else if (kind === 'error') {
    window.web98cert = {
      proceed: function (url) {
        if (typeof url !== 'string') return;
        post(false, 0, 'cert:proceed', JSON.stringify(url.slice(0, 8192)));
      }
    };
  }
})();
)JS";
}

const char* kindName(BridgeKind k) {
  switch (k) {
    case BridgeKind::Chrome: return "chrome";
    case BridgeKind::Dialog: return "dialog";
    case BridgeKind::Startpage: return "startpage";
    case BridgeKind::Error: return "error";
    default: return "none";
  }
}

}  // namespace

void Web98App::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  (void)process_type;
  (void)command_line;
}

void Web98App::OnContextInitialized() {
  AppCore::get().createInitialWindows();
}

void Web98App::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  (void)browser;
  if (!frame->IsMain()) return;
  std::string url = frame->GetURL().ToString();
  BridgeKind kind = classifyFrame(url);
  if (kind == BridgeKind::None) return;

  CefRefPtr<CefV8Value> global = context->GetGlobal();
  CefRefPtr<Web98BridgeHandler> handler = new Web98BridgeHandler();
  CefRefPtr<CefV8Value> post =
      CefV8Value::CreateFunction("__web98_post", handler);
  global->SetValue("__web98_post", post, V8_PROPERTY_ATTRIBUTE_READONLY);

  CefRefPtr<CefV8Value> ret;
  CefRefPtr<CefV8Exception> exc;
  context->Eval(buildShim(kindName(kind)), "web98://bridge", 0, ret, exc);
}

bool Web98App::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  (void)browser;
  if (source_process != PID_BROWSER) return false;
  const std::string name = message->GetName().ToString();
  if (name != "web98:reply" && name != "web98:event") return false;
  CefRefPtr<CefV8Context> ctx = frame->GetV8Context();
  if (!ctx || !ctx->Enter()) return false;
  bool handled = false;
  CefRefPtr<CefV8Value> global = ctx->GetGlobal();
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (name == "web98:reply") {
    CefRefPtr<CefV8Value> fn = global->GetValue("__web98_reply");
    if (fn && fn->IsFunction()) {
      CefV8ValueList call;
      call.push_back(CefV8Value::CreateDouble(args->GetDouble(0)));
      call.push_back(CefV8Value::CreateBool(args->GetBool(1)));
      call.push_back(CefV8Value::CreateString(args->GetString(2)));
      fn->ExecuteFunction(nullptr, call);
      handled = true;
    }
  } else {
    CefRefPtr<CefV8Value> fn = global->GetValue("__web98_event");
    if (fn && fn->IsFunction()) {
      CefV8ValueList call;
      call.push_back(CefV8Value::CreateString(args->GetString(0)));
      call.push_back(CefV8Value::CreateString(args->GetString(1)));
      fn->ExecuteFunction(nullptr, call);
      handled = true;
    }
  }
  ctx->Exit();
  return handled;
}

}  // namespace web98
