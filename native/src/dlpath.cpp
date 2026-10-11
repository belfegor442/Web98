#include "dlpath.h"

#include <windows.h>

#include <regex>

#include "base.h"

namespace web98 {
namespace {

// Canonical case-insensitive comparison base for containment checks.
std::string normalizeForCompare(const std::string& p) {
  std::string s = p;
  for (char& c : s) {
    if (c == '/') c = '\\';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  // strip trailing separators
  while (s.size() > 3 && (s.back() == '\\')) s.pop_back();
  return s;
}

std::string extensionOf(const std::string& name) {
  static const std::regex re("\\.[A-Za-z0-9]{1,12}$");
  std::smatch m;
  if (std::regex_search(name, m, re)) return m[0].str();
  return std::string();
}

}  // namespace

std::string sanitizeFilename(const std::string& name) {
  std::string s;
  if (true) {  // non-strings collapse to "" (caller passes strings only)
    for (char c : name) {
      unsigned char u = static_cast<unsigned char>(c);
      if (u <= 0x1f || u == 0x7f) continue;
      s += c;
    }
  }
  // Keep only the last path segment across both separator styles.
  size_t cut = s.find_last_of("\\/");
  if (cut != std::string::npos) s = s.substr(cut + 1);
  // Replace reserved characters.
  for (char& c : s) {
    if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*')
      c = '_';
  }
  // Strip leading dots, trim.
  size_t first = 0;
  while (first < s.size() && s[first] == '.') ++first;
  s = s.substr(first);
  s = trim(s);
  // Strip trailing dots/spaces.
  while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
  if (s.empty()) s = "download";
  // Reserved device names (match on the stem before the first dot).
  static const std::regex dev("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$",
                              std::regex_constants::icase);
  size_t dot = s.find('.');
  std::string stem = dot == std::string::npos ? s : s.substr(0, dot);
  if (std::regex_match(stem, dev)) s = "_" + s;
  if (s.size() > 120) {
    std::string ext = extensionOf(s);
    s = s.substr(0, 120 - ext.size()) + ext;
  }
  return s;
}

std::string uniquePath(const std::string& dir, const std::string& name) {
  std::string candidate = pathJoin(dir, name);
  if (!fileExists(candidate)) return candidate;
  std::string ext = extensionOf(name);
  std::string stem = name.substr(0, name.size() - ext.size());
  for (int i = 2; i < 10000; ++i) {
    std::string alt = pathJoin(dir, stem + " (" + std::to_string(i) + ")" + ext);
    if (!fileExists(alt)) return alt;
  }
  return candidate;
}

std::string containPath(const std::string& dir, const std::string& candidate) {
  std::string c = normalizeForCompare(candidate);
  std::string d = normalizeForCompare(dir);
  if (c == d) {
    // Path collapsed onto the directory itself: not a file inside it.
    return pathJoin(dir, pathBaseName(candidate));
  }
  std::string prefix = d;
  if (prefix.empty() || prefix.back() != '\\') prefix += '\\';
  if (c.rfind(prefix, 0) == 0) return candidate;
  return pathJoin(dir, pathBaseName(candidate));
}

std::string resolveDownloadPath(const std::string& dir, const std::string& suggestedName) {
  ensureDirTree(dir);
  std::string name = sanitizeFilename(suggestedName);
  std::string full = pathJoin(dir, name);
  full = containPath(dir, full);
  return uniquePath(dir, pathBaseName(full));
}

}  // namespace web98
