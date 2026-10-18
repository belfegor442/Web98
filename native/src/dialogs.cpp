#include "dialogs.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <ole2.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

#include <algorithm>
#include <string>
#include <vector>

#include "base.h"
#include "dlpath.h"
#include "safeurl.h"

#pragma comment(lib, "comdlg32.lib")

namespace web98 {
namespace {

// "JSON (*.json)\0*.json\0\0" from {"json"} ("" exts -> all files).
std::string buildFilter(const std::vector<std::string>& exts,
                        const std::string& label) {
  if (exts.empty()) return std::string("All files\0*.*\0\0", 17);
  std::string patterns;
  for (size_t i = 0; i < exts.size(); ++i) {
    std::string e = exts[i];
    if (!e.empty() && e[0] == '.') e = e.substr(1);
    if (e.empty()) continue;
    if (!patterns.empty()) patterns += ";";
    patterns += "*." + e;
  }
  if (patterns.empty()) return std::string("All files\0*.*\0\0", 17);
  std::string filter;
  std::string head = label.empty() ? patterns : label + " (" + patterns + ")";
  filter.append(head.data(), head.size());
  filter.push_back('\0');
  filter.append(patterns.data(), patterns.size());
  filter.push_back('\0');
  filter.push_back('\0');
  filter.push_back('\0');
  return filter;
}

}  // namespace

namespace dialogs {

int messageBox(HWND owner, const std::string& text,
               const std::string& caption, UINT type) {
  std::wstring wtext = toWide(text);
  std::wstring wcap = toWide(caption);
  int r = ::MessageBoxW(owner, wtext.c_str(), wcap.c_str(),
                        type | MB_TASKMODAL | MB_SETFOREGROUND);
  return r;
}

std::string saveFileDialog(HWND owner, const std::string& title,
                           const std::string& defaultName,
                           const std::vector<std::string>& exts) {
  std::wstring file(2048, L'\0');
  std::string name = sanitizeFilename(defaultName.empty() ? "page.html"
                                                          : defaultName);
  {
    std::wstring wname = toWide(name);
    size_t n = std::min(wname.size(), file.size() - 1);
    for (size_t i = 0; i < n; ++i) file[i] = wname[i];
    file[n] = L'\0';
  }

  std::wstring wtitle = toWide(title);
  std::wstring wext = exts.empty()
                          ? std::wstring()
                          : toWide(exts[0].rfind('.', 0) == 0
                                       ? exts[0]
                                       : std::string("." + exts[0]));
  std::string filterA = buildFilter(exts, std::string());
  std::wstring filterW = toWide(filterA);

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filterW.c_str();
  ofn.lpstrFile = &file[0];
  ofn.nMaxFile = static_cast<DWORD>(file.size());
  ofn.lpstrTitle = wtitle.c_str();
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  // lpstrDefExt appends the extension when the typed name has none.
  if (!exts.empty()) ofn.lpstrDefExt = wext.c_str() + 1;
  if (!::GetSaveFileNameW(&ofn)) return std::string();
  return toUtf8(std::wstring(&file[0]));
}

std::string openFileDialog(HWND owner, const std::string& title,
                           const std::vector<std::string>& exts) {
  std::wstring file(2048, L'\0');
  file[0] = L'\0';
  std::wstring wtitle = toWide(title);
  std::string filterA = buildFilter(exts, std::string());
  std::wstring filterW = toWide(filterA);

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filterW.c_str();
  ofn.lpstrFile = &file[0];
  ofn.nMaxFile = static_cast<DWORD>(file.size());
  ofn.lpstrTitle = wtitle.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!::GetOpenFileNameW(&ofn)) return std::string();
  return toUtf8(std::wstring(&file[0]));
}

std::vector<std::string> openFilesDialog(HWND owner, const std::string& title,
                                         const std::vector<std::string>& exts) {
  // OFN_ALLOWMULTISELECT needs a double-NUL terminated buffer.
  std::wstring buf(64 * 1024, L'\0');
  buf[0] = L'\0';
  std::wstring wtitle = toWide(title);
  std::string filterA = buildFilter(exts, std::string());
  std::wstring filterW = toWide(filterA);

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = filterW.c_str();
  ofn.lpstrFile = &buf[0];
  ofn.nMaxFile = static_cast<DWORD>(buf.size());
  ofn.lpstrTitle = wtitle.c_str();
  ofn.Flags = OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
              OFN_NOCHANGEDIR;
  if (!::GetOpenFileNameW(&ofn)) return {};

  std::vector<std::string> out;
  const wchar_t* p = buf.c_str();
  std::wstring dir(p);
  p += dir.size() + 1;
  if (!*p) {
    out.push_back(toUtf8(dir));
    return out;
  }
  while (*p) {
    std::wstring file(p);
    out.push_back(pathJoin(toUtf8(dir), toUtf8(file)));
    p += file.size() + 1;
  }
  return out;
}

std::string folderDialog(HWND owner, const std::string& title) {
  std::string out;
  IFileOpenDialog* dlg = nullptr;
  HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr)) {
    // Fallback: SHBrowseForFolderW.
    BROWSEINFOW bi{};
    std::wstring wtitle = toWide(title);
    bi.hwndOwner = owner;
    bi.lpszTitle = wtitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = ::SHBrowseForFolderW(&bi);
    if (pidl) {
      wchar_t buf[MAX_PATH] = {};
      if (::SHGetPathFromIDListW(pidl, buf)) out = toUtf8(buf);
      CoTaskMemFree(pidl);
    }
    return out;
  }
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  if (!title.empty()) {
    std::wstring wtitle = toWide(title);
    dlg->SetTitle(wtitle.c_str());
  }
  if (dlg->Show(owner) == S_OK) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
        out = toUtf8(p);
        CoTaskMemFree(p);
      }
      item->Release();
    }
  }
  dlg->Release();
  return out;
}

bool shellOpenPath(const std::string& path) {
  if (path.empty()) return false;
  std::wstring w = toWide(path);
  HINSTANCE r = ::ShellExecuteW(nullptr, L"open", w.c_str(), nullptr,
                                nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(r) > 32;
}

bool showInFolder(const std::string& path) {
  if (path.empty()) return false;
  std::wstring w = toWide(path);
  PIDLIST_ABSOLUTE pidl = nullptr;
  HRESULT hr = ::SHParseDisplayName(w.c_str(), nullptr, &pidl, 0, nullptr);
  if (SUCCEEDED(hr) && pidl) {
    hr = ::SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    CoTaskMemFree(pidl);
    if (SUCCEEDED(hr)) return true;
  }
  // Fallback: explorer /select,"<path>"
  std::wstring arg = L"/select,\"" + w + L"\"";
  HINSTANCE r = ::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                arg.c_str(), nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(r) > 32;
}

bool openExternalUrl(const std::string& url) {
  if (!isSafeExternalUrl(url)) return false;
  std::wstring w = toWide(url);
  HINSTANCE r = ::ShellExecuteW(nullptr, L"open", w.c_str(), nullptr,
                                nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(r) > 32;
}

namespace {

constexpr int kPromptOk = 1001;
constexpr int kPromptCancel = 1002;
constexpr int kPromptEdit = 1003;

struct PromptData {
  std::wstring message;
  std::wstring def;
  std::wstring result;
  bool ok = false;
  HWND edit = nullptr;
};

void promptSetFont(HWND ctl) {
  ::SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  PromptData* d =
      reinterpret_cast<PromptData*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      d = reinterpret_cast<PromptData*>(cs->lpCreateParams);
      ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
      DWORD style = WS_CHILD | WS_VISIBLE;
      HWND label = ::CreateWindowExW(
          0, L"STATIC", d->message.c_str(), style | SS_LEFT,
          12, 10, 396, 70, hwnd, reinterpret_cast<HMENU>(1), cs->hInstance, nullptr);
      HWND edit = ::CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", d->def.c_str(),
          style | ES_AUTOHSCROLL | ES_LEFT | WS_TABSTOP,
          12, 88, 396, 24, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptEdit)),
          cs->hInstance, nullptr);
      HWND ok = ::CreateWindowExW(
          0, L"BUTTON", L"OK",
          style | WS_TABSTOP | BS_DEFPUSHBUTTON,
          236, 124, 84, 26, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptOk)),
          cs->hInstance, nullptr);
      HWND cancel = ::CreateWindowExW(
          0, L"BUTTON", L"Cancel",
          style | WS_TABSTOP,
          324, 124, 84, 26, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptCancel)),
          cs->hInstance, nullptr);
      promptSetFont(label);
      promptSetFont(edit);
      promptSetFont(ok);
      promptSetFont(cancel);
      d->edit = edit;
      ::SendMessageW(edit, EM_SETSEL, 0, -1);
      ::SetFocus(edit);
      return 0;
    }
    case WM_COMMAND: {
      if (!d) return 0;
      if (LOWORD(wp) == kPromptOk) {
        wchar_t buf[4096] = {};
        ::GetWindowTextW(d->edit, buf, 4096);
        d->result = buf;
        d->ok = true;
        ::DestroyWindow(hwnd);
        return 0;
      }
      if (LOWORD(wp) == kPromptCancel) {
        d->ok = false;
        ::DestroyWindow(hwnd);
        return 0;
      }
      return 0;
    }
    case WM_CLOSE:
      if (d) d->ok = false;
      ::DestroyWindow(hwnd);
      return 0;
    case WM_ACTIVATE:
      if (LOWORD(wp) != WA_INACTIVE && d && d->edit) ::SetFocus(d->edit);
      return 0;
    default:
      break;
  }
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

bool ensurePromptClass() {
  static bool done = false;
  static bool ok = false;
  if (done) return ok;
  done = true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = PromptProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"Web98Prompt";
  ok = ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return ok;
}

}  // namespace

bool promptDialog(HWND owner, const std::string& title,
                  const std::string& message, const std::string& defaultValue,
                  std::string* out) {
  if (!ensurePromptClass()) return false;
  PromptData d;
  d.message = toWide(message);
  d.def = toWide(defaultValue);
  std::wstring wtitle = toWide(title.empty() ? "Web98" : title);

  const int w = 430, h = 190;
  int x = 0, y = 0;
  if (owner) {
    RECT rc{};
    ::GetWindowRect(owner, &rc);
    x = rc.left + ((rc.right - rc.left) - w) / 2;
    y = rc.top + ((rc.bottom - rc.top) - h) / 2;
  } else {
    x = (::GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    y = (::GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  }

  HWND hwnd = ::CreateWindowExW(WS_EX_DLGMODALFRAME, L"Web98Prompt",
                                wtitle.c_str(),
                                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                x, y, w, h, owner, nullptr,
                                ::GetModuleHandleW(nullptr), &d);
  if (!hwnd) return false;
  ::ShowWindow(hwnd, SW_SHOW);
  ::UpdateWindow(hwnd);
  if (owner) ::EnableWindow(owner, FALSE);

  MSG msg;
  while (::IsWindow(hwnd) && ::GetMessageW(&msg, nullptr, 0, 0)) {
    if (!::IsDialogMessageW(hwnd, &msg)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
  }
  if (owner) {
    ::EnableWindow(owner, TRUE);
    ::SetActiveWindow(owner);
  }
  if (d.ok && out) *out = toUtf8(d.result);
  return d.ok;
}

namespace {

constexpr int kAuthOk = 1101;
constexpr int kAuthCancel = 1102;
constexpr int kAuthUser = 1103;
constexpr int kAuthPass = 1104;

struct AuthData {
  std::wstring message;
  std::wstring user;
  std::wstring pass;
  bool ok = false;
  HWND userEdit = nullptr;
  HWND passEdit = nullptr;
};

void authSetFont(HWND ctl) {
  ::SendMessageW(ctl, WM_SETFONT,
                 reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
}

LRESULT CALLBACK AuthProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  AuthData* d =
      reinterpret_cast<AuthData*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      d = reinterpret_cast<AuthData*>(cs->lpCreateParams);
      ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
      DWORD style = WS_CHILD | WS_VISIBLE;
      HWND label = ::CreateWindowExW(
          0, L"STATIC", d->message.c_str(), style | SS_LEFT, 12, 10, 396, 56,
          hwnd, reinterpret_cast<HMENU>(1), cs->hInstance, nullptr);
      HWND userLabel = ::CreateWindowExW(
          0, L"STATIC", L"User name:", style | SS_LEFT, 12, 74, 90, 20, hwnd,
          reinterpret_cast<HMENU>(2), cs->hInstance, nullptr);
      HWND userEdit = ::CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          style | ES_AUTOHSCROLL | ES_LEFT | WS_TABSTOP, 12, 94, 396, 24, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAuthUser)),
          cs->hInstance, nullptr);
      HWND passLabel = ::CreateWindowExW(
          0, L"STATIC", L"Password:", style | SS_LEFT, 12, 124, 90, 20, hwnd,
          reinterpret_cast<HMENU>(3), cs->hInstance, nullptr);
      HWND passEdit = ::CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          style | ES_AUTOHSCROLL | ES_LEFT | WS_TABSTOP | ES_PASSWORD, 12, 144,
          396, 24, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAuthPass)),
          cs->hInstance, nullptr);
      HWND ok = ::CreateWindowExW(
          0, L"BUTTON", L"OK",
          style | WS_TABSTOP | BS_DEFPUSHBUTTON, 236, 180, 84, 26, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAuthOk)),
          cs->hInstance, nullptr);
      HWND cancel = ::CreateWindowExW(
          0, L"BUTTON", L"Cancel", style | WS_TABSTOP, 324, 180, 84, 26, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAuthCancel)),
          cs->hInstance, nullptr);
      authSetFont(label);
      authSetFont(userLabel);
      authSetFont(userEdit);
      authSetFont(passLabel);
      authSetFont(passEdit);
      authSetFont(ok);
      authSetFont(cancel);
      d->userEdit = userEdit;
      d->passEdit = passEdit;
      ::SetFocus(userEdit);
      return 0;
    }
    case WM_COMMAND: {
      if (!d) return 0;
      if (LOWORD(wp) == kAuthOk) {
        wchar_t ubuf[1024] = {};
        wchar_t pbuf[1024] = {};
        ::GetWindowTextW(d->userEdit, ubuf, 1024);
        ::GetWindowTextW(d->passEdit, pbuf, 1024);
        d->user = ubuf;
        d->pass = pbuf;
        d->ok = true;
        ::DestroyWindow(hwnd);
        return 0;
      }
      if (LOWORD(wp) == kAuthCancel) {
        d->ok = false;
        ::DestroyWindow(hwnd);
        return 0;
      }
      return 0;
    }
    case WM_CLOSE:
      if (d) d->ok = false;
      ::DestroyWindow(hwnd);
      return 0;
    case WM_ACTIVATE:
      if (LOWORD(wp) != WA_INACTIVE && d && d->userEdit)
        ::SetFocus(d->userEdit);
      return 0;
    default:
      break;
  }
  return ::DefWindowProcW(hwnd, msg, wp, lp);
}

bool ensureAuthClass() {
  static bool done = false;
  static bool ok = false;
  if (done) return ok;
  done = true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AuthProc;
  wc.hInstance = ::GetModuleHandleW(nullptr);
  wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"Web98Auth";
  ok = ::RegisterClassExW(&wc) != 0 ||
       ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return ok;
}

}  // namespace

bool authDialog(HWND owner, const std::string& title,
                const std::string& message, std::string* user,
                std::string* pass) {
  if (!ensureAuthClass()) return false;
  AuthData d;
  d.message = toWide(message);
  std::wstring wtitle = toWide(title.empty() ? "Web98" : title);

  const int w = 430, h = 250;
  int x = 0, y = 0;
  if (owner) {
    RECT rc{};
    ::GetWindowRect(owner, &rc);
    x = rc.left + ((rc.right - rc.left) - w) / 2;
    y = rc.top + ((rc.bottom - rc.top) - h) / 2;
  } else {
    x = (::GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    y = (::GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  }

  HWND hwnd = ::CreateWindowExW(WS_EX_DLGMODALFRAME, L"Web98Auth",
                                 wtitle.c_str(),
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w,
                                 h, owner, nullptr,
                                 ::GetModuleHandleW(nullptr), &d);
  if (!hwnd) return false;
  ::ShowWindow(hwnd, SW_SHOW);
  ::UpdateWindow(hwnd);
  if (owner) ::EnableWindow(owner, FALSE);

  MSG msg;
  while (::IsWindow(hwnd) && ::GetMessageW(&msg, nullptr, 0, 0)) {
    if (!::IsDialogMessageW(hwnd, &msg)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
  }
  if (owner) {
    ::EnableWindow(owner, TRUE);
    ::SetActiveWindow(owner);
  }
  if (d.ok) {
    if (user) *user = toUtf8(d.user);
    if (pass) *pass = toUtf8(d.pass);
  }
  return d.ok;
}

void setClipboardText(const std::string& text) {
  if (!::OpenClipboard(nullptr)) return;
  ::EmptyClipboard();
  std::wstring w = toWide(text);
  size_t bytes = (w.size() + 1) * sizeof(wchar_t);
  HGLOBAL h = static_cast<HGLOBAL>(::GlobalAlloc(GMEM_MOVEABLE, bytes));
  if (h) {
    void* p = ::GlobalLock(h);
    if (p) {
      memcpy(p, w.c_str(), bytes);
      ::GlobalUnlock(h);
      ::SetClipboardData(CF_UNICODETEXT, h);
    } else {
      ::GlobalFree(h);
    }
  }
  ::CloseClipboard();
}

std::string getClipboardText() {
  std::string out;
  if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) return out;
  if (!::OpenClipboard(nullptr)) return out;
  HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
  if (h) {
    const wchar_t* p = static_cast<const wchar_t*>(::GlobalLock(h));
    if (p) {
      out = toUtf8(std::wstring(p));
      ::GlobalUnlock(h);
    }
  }
  ::CloseClipboard();
  return out;
}

bool setClipboardImage(const void* data, size_t len) {
  if (!data || !len) return false;
  // Init GDI+ once (same lazy pattern as the PNG snapshot encoder).
  static ULONG_PTR s_token = 0;
  static bool s_initTried = false;
  if (!s_initTried) {
    s_initTried = true;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&s_token, &input, nullptr) != Gdiplus::Ok)
      s_token = 0;
  }
  if (!s_token) return false;

  // Stage the bytes in an HGLOBAL-backed IStream for GDI+ to decode.
  HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, len);
  if (!hg) return false;
  void* p = ::GlobalLock(hg);
  if (!p) {
    ::GlobalFree(hg);
    return false;
  }
  memcpy(p, data, len);
  ::GlobalUnlock(hg);

  bool ok = false;
  IStream* stream = nullptr;
  // FALSE => we keep ownership of hg and free it below.
  if (::CreateStreamOnHGlobal(hg, FALSE, &stream) == S_OK && stream) {
    Gdiplus::Bitmap bmp(stream, FALSE);
    if (bmp.GetLastStatus() == Gdiplus::Ok) {
      HBITMAP hbmp = nullptr;
      if (bmp.GetHBITMAP(Gdiplus::Color(), &hbmp) == Gdiplus::Ok && hbmp) {
        BITMAP bm;
        if (::GetObject(hbmp, sizeof(bm), &bm) && bm.bmWidth > 0 &&
            bm.bmHeight > 0) {
          BITMAPINFOHEADER bi = {};
          bi.biSize = sizeof(bi);
          bi.biWidth = bm.bmWidth;
          bi.biHeight = -bm.bmHeight;  // top-down
          bi.biPlanes = 1;
          bi.biBitCount = 32;
          bi.biCompression = BI_RGB;
          size_t stride = ((static_cast<size_t>(bm.bmWidth) * 32 + 31) / 32) * 4;
          size_t pixLen = stride * static_cast<size_t>(bm.bmHeight);
          std::vector<unsigned char> pixels(pixLen);
          HDC hdc = ::GetDC(nullptr);
          if (hdc &&
              ::GetDIBits(hdc, hbmp, 0, static_cast<UINT>(bm.bmHeight),
                          pixels.data(),
                          reinterpret_cast<BITMAPINFO*>(&bi),
                          DIB_RGB_COLORS)) {
            size_t total = sizeof(BITMAPINFOHEADER) + pixLen;
            HGLOBAL hclip = ::GlobalAlloc(GMEM_MOVEABLE, total);
            if (hclip) {
              void* cp = ::GlobalLock(hclip);
              if (cp) {
                memcpy(cp, &bi, sizeof(bi));
                memcpy(static_cast<char*>(cp) + sizeof(bi), pixels.data(),
                       pixLen);
                ::GlobalUnlock(hclip);
                if (::OpenClipboard(nullptr)) {
                  ::EmptyClipboard();
                  if (::SetClipboardData(CF_DIB, hclip)) ok = true;
                  ::CloseClipboard();
                }
                if (!ok) ::GlobalFree(hclip);
              } else {
                ::GlobalFree(hclip);
              }
            }
          }
          if (hdc) ::ReleaseDC(nullptr, hdc);
        }
        ::DeleteObject(hbmp);
      }
    }
    stream->Release();
  }
  ::GlobalFree(hg);
  return ok;
}

}  // namespace dialogs
}  // namespace web98
