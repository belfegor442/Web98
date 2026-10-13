#include "base.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "urlbar.h"  // encodeURIComponent

namespace web98 {

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                              nullptr, 0, nullptr, nullptr);
  if (n <= 0) return std::string();
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                      &out[0], n, nullptr, nullptr);
  return out;
}

std::wstring toWide(const std::string& u) {
  if (u.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), static_cast<int>(u.size()),
                              nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u.c_str(), static_cast<int>(u.size()),
                      &out[0], n);
  return out;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r' ||
                   s[a] == '\f' || s[a] == '\v'))
    ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' ||
                   s[b - 1] == '\r' || s[b - 1] == '\f' || s[b - 1] == '\v'))
    --b;
  return s.substr(a, b - a);
}

std::string asciiLower(const std::string& s) {
  std::string out = s;
  for (char& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool fileExists(const std::string& path) {
  DWORD a = GetFileAttributesW(toWide(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::string& path) {
  DWORD a = GetFileAttributesW(toWide(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ensureDirTree(const std::string& path) {
  if (path.empty()) return false;
  if (dirExists(path)) return true;
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    char c = path[i];
    cur += c;
    if ((c == '\\' || c == '/') && cur.size() > 3) {
      std::string part = cur.substr(0, cur.size() - 1);
      if (!dirExists(part)) CreateDirectoryW(toWide(part).c_str(), nullptr);
    }
  }
  if (!dirExists(path)) CreateDirectoryW(toWide(path).c_str(), nullptr);
  return dirExists(path);
}

bool readFile(const std::string& path, std::string& out) {
  HANDLE h = CreateFileW(toWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (64ll << 20)) {
    CloseHandle(h);
    return false;
  }
  out.assign(static_cast<size_t>(sz.QuadPart), '\0');
  DWORD read = 0;
  bool ok = sz.QuadPart == 0 ||
            (ReadFile(h, &out[0], static_cast<DWORD>(sz.QuadPart), &read, nullptr) &&
             read == static_cast<DWORD>(sz.QuadPart));
  CloseHandle(h);
  if (!ok) out.clear();
  return ok;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
  if (data.size() > static_cast<size_t>(MAXDWORD)) return false;

  static volatile LONG sequence = 0;
  const LONG id = ::InterlockedIncrement(&sequence);
  const std::string tmp = path + ".tmp." +
      std::to_string(::GetCurrentProcessId()) + "." + std::to_string(id);
  const std::wstring tmpWide = toWide(tmp);
  const std::wstring pathWide = toWide(path);
  HANDLE h = CreateFileW(tmpWide.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;

  DWORD written = 0;
  bool ok = data.empty() ||
            (WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written,
                       nullptr) &&
             written == static_cast<DWORD>(data.size()));
  if (ok) ok = FlushFileBuffers(h) != FALSE;
  if (!CloseHandle(h)) ok = false;
  if (!ok) {
    DeleteFileW(tmpWide.c_str());
    return false;
  }

  if (!MoveFileExW(tmpWide.c_str(), pathWide.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmpWide.c_str());
    return false;
  }
  return true;
}

std::string pathJoin(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  char last = a[a.size() - 1];
  if (last == '\\' || last == '/') return a + b;
  return a + "\\" + b;
}

std::string pathDirName(const std::string& p) {
  size_t at = p.find_last_of("\\/");
  if (at == std::string::npos) return "";
  return p.substr(0, at);
}

std::string pathBaseName(const std::string& p) {
  size_t at = p.find_last_of("\\/");
  return at == std::string::npos ? p : p.substr(at + 1);
}

bool isAbsolutePathNative(const std::string& p) {
  if (p.size() >= 3 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
      p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
    return true;
  if (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/'))
    return true;
  return false;
}

std::string exeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring w(buf, n);
  size_t at = w.find_last_of(L"\\/");
  return toUtf8(at == std::wstring::npos ? w : w.substr(0, at));
}

std::string envOrEmpty(const char* name) {
  wchar_t buf[2048];
  DWORD n = GetEnvironmentVariableW(toWide(name).c_str(), buf, 2048);
  if (n == 0 || n >= 2048) return std::string();
  return toUtf8(std::wstring(buf, n));
}

std::string pathToFileUrl(const std::string& path) {
  std::string norm = path;
  for (char& c : norm)
    if (c == '\\') c = '/';
  std::string enc;
  for (unsigned char c : norm) {
    // Keep unreserved characters, '/', and ':' (drive letter) literal.
    // Everything else — including non-ASCII bytes and reserved chars like
    // < > ` { } | ^ [ ] — must be percent-encoded or CEF mis-parses the URL.
    bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' ||
                c == '.' || c == '_' || c == '~';
    if (safe) {
      enc += static_cast<char>(c);
    } else {
      char b[4];
      std::snprintf(b, sizeof(b), "%%%02X", c);
      enc += b;
    }
  }
  if (enc.rfind("/", 0) == 0) enc = "/" + enc;  // keep leading slash
  if (enc.size() >= 2 && enc[0] == '/' && enc[2] == ':') {
    // /C:/... -> file:///C:/...
  }
  return "file:///" + enc;
}

std::string fileUrlToPath(const std::string& url) {
  if (!startsWith(url, "file:")) return std::string();
  std::string rest = url.substr(5);
  while (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
  // strip query/fragment (file URLs in pages may carry them)
  size_t cut = rest.find_first_of("?#");
  if (cut != std::string::npos) rest = rest.substr(0, cut);
  std::string out;
  for (size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] == '%' && i + 2 < rest.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int hi = hex(rest[i + 1]), lo = hex(rest[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    out += rest[i];
  }
  return out;
}

std::string userDataDir() {
  std::string env = envOrEmpty("WEB98_USER_DATA");
  if (!env.empty()) return env;
  PWSTR p = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) {
    out = pathJoin(toUtf8(p), "Web98");
    CoTaskMemFree(p);
  } else {
    out = pathJoin(envOrEmpty("APPDATA"), "Web98");
  }
  ensureDirTree(out);
  return out;
}

std::string defaultDownloadsDir() {
  PWSTR p = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p)) && p) {
    out = toUtf8(p);
    CoTaskMemFree(p);
  }
  if (out.empty()) out = pathJoin(envOrEmpty("USERPROFILE"), "Downloads");
  return out;
}

std::string resourceDir() {
  static std::string cached;
  if (!cached.empty()) return cached;
  std::vector<std::string> candidates;
  wchar_t ebuf[2048];
  DWORD n = GetEnvironmentVariableW(L"WEB98_RES_DIR", ebuf, 2048);
  if (n > 0 && n < 2048) candidates.push_back(toUtf8(std::wstring(ebuf, n)));
  std::string ed = exeDir();
  // Installed layout: <exe>/res/src/renderer (+ sibling res/assets, res/css).
  candidates.push_back(pathJoin(pathJoin(ed, "res"), "src\\renderer"));
  // Repo layout: native/build/<config>/exe -> ../../../src/renderer.
  candidates.push_back(pathJoin(pathJoin(pathJoin(ed, ".."), "..\\.."), "src\\renderer"));
  candidates.push_back(pathJoin(pathJoin(pathJoin(ed, ".."), "..\\..\\.."), "src\\renderer"));
  candidates.push_back(pathJoin(pathJoin(pathJoin(ed, ".."), "..\\..\\..\\.."), "src\\renderer"));
  // Legacy flat res/ copy (startpage.html directly inside).
  candidates.push_back(pathJoin(ed, "res"));
  for (const auto& c : candidates) {
    if (fileExists(pathJoin(c, "startpage.html"))) {
      cached = c;
      return cached;
    }
  }
  cached = candidates[0];
  return cached;
}

std::string homepageUrl() { return pathToFileUrl(pathJoin(resourceDir(), "startpage.html")); }
std::string errorPageUrl() { return pathToFileUrl(pathJoin(resourceDir(), "error.html")); }

int64_t nowMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  // 100ns intervals since 1601 -> ms since epoch
  return static_cast<int64_t>(u.QuadPart / 10000ull) - 11644473600000ll;
}

std::string urlEncodeQuery(const std::string& s) { return encodeURIComponent(s); }

}  // namespace web98
