// Entry point: single-instance guard, CEF bootstrap, message loop.
// With USE_SANDBOX this is the client DLL loaded by bootstrap.exe, which
// calls RunWinMain; without it, plain wWinMain.
#include <windows.h>
#include <objbase.h>

#include <string>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_sandbox_win.h"

#include "app_core.h"
#include "base.h"
#include "cef_app.h"

namespace web98 {
namespace {

// Electron main.js single-instance semantics: focus the live frame instead.
bool acquireSingleInstance() {
  std::wstring name = L"Local\\Web98-" + toWide(userDataDir());
  HANDLE mutex = ::CreateMutexW(nullptr, TRUE, name.c_str());
  if (!mutex) return true;  // cannot create: continue rather than brick
  if (::GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = ::FindWindowW(L"Web98Frame", nullptr);
    if (existing) {
      if (::IsIconic(existing)) ::ShowWindow(existing, SW_RESTORE);
      ::SetForegroundWindow(existing);
    }
    ::CloseHandle(mutex);
    return false;
  }
  return true;  // keep the mutex alive for the process lifetime
}

int runBrowser(HINSTANCE instance, LPWSTR cmd_line, int show_cmd,
               void* sandbox_info, bool sandboxed) {
  (void)show_cmd;
  (void)cmd_line;
  CefMainArgs main_args(instance);
  // Subprocesses (renderer/gpu/utility) re-enter through CefExecuteProcess.
  // They need the same CefApp so the render process gets the V8 bridge.
  CefRefPtr<Web98App> app = new Web98App();
  int code = CefExecuteProcess(main_args, app.get(), sandbox_info);
  if (code >= 0) return code;

  // Only the browser's main process owns the single-instance mutex. CEF
  // renderer/GPU/utility subprocesses must reach CefExecuteProcess first.
  if (!acquireSingleInstance()) return 0;

  // Shell APIs (SHGetKnownFolderPath, ShellExecute) and CEF require COM.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  if (!AppCore::get().init()) {
    ::CoUninitialize();
    return 1;
  }

  CefSettings settings;
  // Sandbox off: the sandbox bootstrap intercepts renderer/GPU subprocess
  // launch and never calls RunWinMain in our DLL, so the V8 bridge (render
  // process handler) never loads. no_sandbox routes all subprocesses through
  // CefExecuteProcess in this binary, which passes our CefApp.
  settings.no_sandbox = 1;
  // Single Win32 message loop: WndProcs and CEF callbacks share the thread.
  settings.multi_threaded_message_loop = 0;
  // No per-process log files: Chromium logging adds constant small writes in
  // the browser, renderer, GPU and utility processes (subprocesses inherit
  // the disabled state, so --enable-logging disappears from their command
  // lines too). Diagnostics come from the unit tests instead.
  settings.log_severity = LOGSEVERITY_DISABLE;
  CefString(&settings.cache_path) = pathJoin(userDataDir(), "Cache");
  CefString(&settings.root_cache_path) = pathJoin(userDataDir(), "Cache");
  CefString(&settings.user_agent_product) = "Web98";

  if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
    AppCore::get().shutdown();
    ::CoUninitialize();
    return CefGetExitCode();
  }
  // Initial windows are created from Web98App::OnContextInitialized.
  CefRunMessageLoop();
  AppCore::get().shutdown();
  CefShutdown();
  ::CoUninitialize();
  return 0;
}

}  // namespace
}  // namespace web98

#if defined(CEF_USE_BOOTSTRAP)

extern "C" CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,
                                               LPWSTR cmd_line, int show_cmd,
                                               void* sandbox_info,
                                               cef_version_info_t* version_info) {
  (void)version_info;
  return web98::runBrowser(instance, cmd_line, show_cmd, sandbox_info,
                           /*sandboxed=*/true);
}

#else  // !CEF_USE_BOOTSTRAP

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE prev_instance,
                      LPWSTR cmd_line, int show_cmd) {
  UNREFERENCED_PARAMETER(prev_instance);
  void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif
  return web98::runBrowser(instance, cmd_line, show_cmd, sandbox_info,
                           /*sandboxed=*/false);
}

#endif  // CEF_USE_BOOTSTRAP
