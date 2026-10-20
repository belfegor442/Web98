#include "passwords.h"

#include <windows.h>
#include <dpapi.h>

#include "base.h"

namespace web98 {
namespace {

std::vector<uint8_t> runDpapi(bool protect, const std::vector<uint8_t>& in) {
  if (in.empty()) return {};
  DATA_BLOB raw;
  raw.pbData = const_cast<BYTE*>(in.data());
  raw.cbData = static_cast<DWORD>(in.size());
  DATA_BLOB out{};
  BOOL ok =
      protect ? CryptProtectData(&raw, L"Web98", nullptr, nullptr, nullptr,
                                 CRYPTPROTECT_UI_FORBIDDEN, &out)
              : CryptUnprotectData(&raw, nullptr, nullptr, nullptr, nullptr,
                                   CRYPTPROTECT_UI_FORBIDDEN, &out);
  if (!ok || !out.pbData) return {};
  std::vector<uint8_t> res(out.pbData, out.pbData + out.cbData);
  ::LocalFree(out.pbData);
  return res;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

std::vector<uint8_t> dpapiProtect(const std::vector<uint8_t>& plain) {
  return runDpapi(true, plain);
}

std::vector<uint8_t> dpapiUnprotect(const std::vector<uint8_t>& blob) {
  return runDpapi(false, blob);
}

std::string originFromUrl(const std::string& url) {
  if (url.empty() || url.size() > 8192) return {};
  for (unsigned char c : url)
    if (c < 0x20 || c == 0x7f) return {};
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos || schemeEnd == 0) return {};
  std::string scheme = asciiLower(url.substr(0, schemeEnd));
  if (scheme != "http" && scheme != "https") return {};
  size_t hostStart = schemeEnd + 3;
  if (hostStart >= url.size()) return {};
  size_t hostEnd = url.find_first_of("/?#", hostStart);
  std::string authority =
      hostEnd == std::string::npos ? url.substr(hostStart)
                                   : url.substr(hostStart, hostEnd - hostStart);
  size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);
  if (authority.empty()) return {};
  std::string host, port;
  if (authority[0] == '[') {
    size_t close = authority.find(']');
    if (close == std::string::npos || close == 1) return {};
    host = authority.substr(0, close + 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':') return {};
      port = authority.substr(close + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      if (authority.find(':') != colon) return {};  // bare multi-colon host
      host = authority.substr(0, colon);
      port = authority.substr(colon + 1);
    } else {
      host = authority;
    }
  }
  if (host.empty()) return {};
  if (host.find_first_of(" \t/?#@") != std::string::npos) return {};
  if (!port.empty()) {
    if (port.size() > 5) return {};
    for (char c : port)
      if (!isDigit(c)) return {};
    if ((scheme == "http" && port == "80") ||
        (scheme == "https" && port == "443")) {
      port.clear();
    }
  }
  host = asciiLower(host);
  return scheme + "://" + host + (port.empty() ? std::string() : ":" + port);
}

}  // namespace web98
