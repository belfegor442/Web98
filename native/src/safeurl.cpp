#include "safeurl.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace web98 {
namespace {

std::string trimAscii(const std::string& s) {
  size_t a = 0, b = s.size();
  auto isSp = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  };
  while (a < b && isSp(s[a])) ++a;
  while (b > a && isSp(s[b - 1])) --b;
  return s.substr(a, b - a);
}

bool hasControl(const std::string& s) {
  for (unsigned char c : s)
    if ((c >= 0x00 && c <= 0x1F) || c == 0x7F) return true;
  return false;
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool isViewSource(const std::string& rest) {  // recursive, !fromWeb form
  return isLoadableUrl(rest);
}

}  // namespace

std::string schemeOf(const std::string& url) {
  std::string s = trimAscii(url);
  if (s.empty() || s.size() > kMaxUrlLen || hasControl(s)) return "";
  size_t i = 0;
  if (!std::isalpha(static_cast<unsigned char>(s[0]))) return "";
  ++i;
  while (i < s.size()) {
    char c = s[i];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.')
      ++i;
    else
      break;
  }
  if (i >= s.size() || s[i] != ':') return "";
  return toLower(s.substr(0, i + 1));
}

bool isLoadableUrl(const std::string& url) {
  std::string scheme = schemeOf(url);
  if (scheme.empty()) return false;
  if (scheme == "http:" || scheme == "https:" || scheme == "ftp:" || scheme == "file:" ||
      scheme == "data:")
    return true;
  if (scheme == "about:") {
    std::string rest = trimAscii(url).substr(6);
    return toLower(rest) == "blank";
  }
  if (scheme == "view-source:") {
    std::string rest = trimAscii(url).substr(12);
    return isViewSource(rest);
  }
  return false;  // javascript:, chrome:, devtools:, blob:, mailto:, tel:, garbage
}

bool isWebUrl(const std::string& url) {
  std::string s = schemeOf(url);
  return s == "http:" || s == "https:" || s == "ftp:" || s == "blob:";
}

bool isAllowedNavigation(const std::string& url, bool fromWeb) {
  std::string scheme = schemeOf(url);
  if (scheme.empty()) return false;
  if (scheme == "http:" || scheme == "https:" || scheme == "ftp:") return true;
  if (scheme == "about:") {
    std::string rest = trimAscii(url).substr(6);
    return toLower(rest) == "blank";
  }
  if (scheme == "blob:") return fromWeb;
  if (scheme == "file:") return !fromWeb;
  if (scheme == "view-source:") {
    std::string rest = trimAscii(url).substr(12);
    return isAllowedNavigation(rest, fromWeb);
  }
  return false;
}

bool isSafeExternalUrl(const std::string& url) {
  if (url.size() > 2048) return false;
  if (url != trimAscii(url)) return false;  // no leading/trailing whitespace
  static const std::regex re("^(mailto|tel):", std::regex_constants::icase);
  if (!std::regex_search(url, re)) return false;
  if (hasControl(url)) return false;
  size_t colon = url.find(':');
  if (colon + 1 >= url.size()) return false;  // non-empty remainder
  return true;
}

bool isBookmarkUrl(const std::string& url) {
  std::string s = schemeOf(url);
  return s == "http:" || s == "https:" || s == "ftp:" || s == "file:";
}

std::string originOf(const std::string& url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos || scheme == 0) return "null";
  const size_t authorityStart = scheme + 3;
  if (authorityStart >= url.size()) return "null";
  const size_t authorityEnd = url.find_first_of("/?#", authorityStart);
  std::string origin = url.substr(0, authorityEnd == std::string::npos
                                        ? std::string::npos
                                        : authorityEnd);
  for (size_t i = 0; i < scheme; ++i) {
    if (origin[i] >= 'A' && origin[i] <= 'Z')
      origin[i] = static_cast<char>(origin[i] - 'A' + 'a');
  }
  for (size_t i = authorityStart; i < origin.size(); ++i) {
    if (origin[i] >= 'A' && origin[i] <= 'Z')
      origin[i] = static_cast<char>(origin[i] - 'A' + 'a');
  }
  return origin;
}

bool isSearchTemplate(const std::string& t) {
  if (t.empty() || t.size() > 500) return false;
  static const std::regex re("^https?://", std::regex_constants::icase);
  return std::regex_search(t, re);
}

std::vector<std::string> sanitizeSessionList(const std::vector<std::string>& list, int max) {
  std::vector<std::string> out;
  int limit = std::max(1, std::min(max > 0 ? max : 50, 200));
  for (const auto& u : list) {
    if (static_cast<int>(out.size()) >= limit) break;
    if (isLoadableUrl(u)) out.push_back(u);
  }
  return out;
}

}  // namespace web98
