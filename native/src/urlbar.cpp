#include "urlbar.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace web98 {
namespace {

std::string trimAll(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

bool containsSpace(const std::string& s) {
  return std::any_of(s.begin(), s.end(),
                     [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

const char* kKnownSchemes[] = {"http", "https", "file",   "ftp",   "about",
                               "mailto", "tel",  "javascript", "view-source", "data"};

bool knownScheme(const std::string& s) {
  for (const char* k : kKnownSchemes)
    if (s == k) return true;
  return false;
}

}  // namespace

std::string encodeURIComponent(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  auto keep = [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' ||
           c == '*' || c == '\'' || c == '(' || c == ')';
  };
  std::string out;
  for (unsigned char c : s) {
    if (keep(c)) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string buildSearchUrl(const std::string& tmpl, const std::string& term) {
  std::string encoded = encodeURIComponent(term);
  size_t at = tmpl.find("%s");
  if (at != std::string::npos) {
    std::string out = tmpl;
    out.replace(at, 2, encoded);
    return out;
  }
  return tmpl + encoded;
}

std::string searchTemplateBase(const std::string& tmpl) {
  size_t at = tmpl.find("%s");
  std::string base = at != std::string::npos ? tmpl.substr(0, at) : tmpl;
  while (!base.empty() && (base.back() == '?' || base.back() == '&' || base.back() == '='))
    base.pop_back();
  return base;
}

AddressResult parseAddress(const std::string& raw, const std::string& searchTemplate,
                           const std::string& defaultScheme) {
  AddressResult r;
  std::string s = trimAll(raw);
  if (s.empty()) {
    r.type = AddressType::Empty;
    return r;
  }

  // Known scheme?
  std::string scheme;
  {
    static const std::regex re("^(?:http|https|file|ftp|about|mailto|tel|javascript|view-source|data):",
                               std::regex_constants::icase);
    if (std::regex_search(s, re)) {
      size_t colon = s.find(':');
      scheme = toLower(s.substr(0, colon));
    }
  }
  if (!scheme.empty() && knownScheme(scheme)) {
    if (scheme == "javascript") {
      r.type = AddressType::Javascript;
      r.url = s.substr(s.find(':') + 1);
      return r;
    }
    if (scheme == "about") {
      if (toLower(s) == "about:blank") {
        r.type = AddressType::Url;
        r.url = s;
        return r;
      }
      // any other about:xxx falls through to search (JS behavior)
    } else {
      r.type = AddressType::Url;
      r.url = s;
      return r;
    }
  }

  // No usable scheme: localhost / IPv4 / hostish => URL, else search.
  auto doSearch = [&]() {
    r.type = AddressType::Search;
    r.term = s;
    r.url = buildSearchUrl(searchTemplate, s);
    return r;
  };
  if (containsSpace(s)) return doSearch();

  std::string authority = s.substr(0, s.find('/'));
  size_t colon = authority.find(':');
  std::string hostPart = colon != std::string::npos ? authority.substr(0, colon) : authority;
  bool hasPort = colon != std::string::npos;

  static const std::regex ipv4Re("^\\d{1,3}(\\.\\d{1,3}){3}$");
  if (s.rfind("localhost", 0) == 0 || std::regex_match(hostPart, ipv4Re)) {
    r.type = AddressType::Url;
    r.url = defaultScheme + "://" + s;
    return r;
  }
  static const std::regex hostishRe("^[a-z0-9][a-z0-9-]*(\\.[a-z0-9-]+)+$",
                                    std::regex_constants::icase);
  if (std::regex_match(hostPart, hostishRe)) {
    r.type = AddressType::Url;
    r.url = "http://" + s;
    return r;
  }
  if (!hasPort && !hostPart.empty() && hostPart.find('.') == std::string::npos)
    return doSearch();  // bare word => search
  return doSearch();
}

}  // namespace web98
