#include "bookmarks_html.h"

#include <cstring>
#include <map>

#include "base.h"

namespace web98 {
namespace {

constexpr size_t kMaxParseNodes = 10000;
constexpr size_t kMaxNameLen = 512;
constexpr size_t kMaxUrlLen = 8192;

std::string escapeHtml(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

bool noCaseEq(const std::string& hay, size_t pos, const char* needle) {
  size_t len = std::strlen(needle);
  if (pos + len > hay.size()) return false;
  for (size_t k = 0; k < len; ++k) {
    char a = hay[pos + k];
    char b = needle[k];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
    if (a != b) return false;
  }
  return true;
}

size_t findNoCase(const std::string& hay, const char* needle, size_t from) {
  size_t len = std::strlen(needle);
  if (len == 0 || hay.size() < len) return std::string::npos;
  for (size_t i = from; i + len <= hay.size(); ++i)
    if (noCaseEq(hay, i, needle)) return i;
  return std::string::npos;
}

size_t findTagEnd(const std::string& s, size_t lt) {
  char quote = 0;
  for (size_t i = lt + 1; i < s.size(); ++i) {
    char c = s[i];
    if (quote) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '>') return i;
  }
  return std::string::npos;
}

// Case-insensitive attribute lookup on the raw (case-preserved) tag text.
// Returns the entity-decoded attribute value (empty when absent).
std::string attrValue(const std::string& tag, const char* name) {
  size_t nlen = std::strlen(name);
  auto alnumish = [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
  };
  for (size_t i = 0; i + nlen <= tag.size(); ++i) {
    if (i > 0) {
      char p = tag[i - 1];
      if (alnumish(p) || p == ':' || p == '.') continue;
    }
    if (!noCaseEq(tag, i, name)) continue;
    size_t j = i + nlen;
    if (j < tag.size() && alnumish(tag[j])) continue;  // longer name
    while (j < tag.size() && (tag[j] == ' ' || tag[j] == '\t')) ++j;
    if (j >= tag.size() || tag[j] != '=') continue;
    ++j;
    while (j < tag.size() && (tag[j] == ' ' || tag[j] == '\t')) ++j;
    if (j >= tag.size()) return std::string();
    if (tag[j] == '"' || tag[j] == '\'') {
      char q = tag[j++];
      size_t end = tag.find(q, j);
      if (end == std::string::npos) return bmDecodeEntities(tag.substr(j));
      return bmDecodeEntities(tag.substr(j, end - j));
    }
    size_t end = j;
    while (end < tag.size() && tag[end] != ' ' && tag[end] != '\t') ++end;
    return bmDecodeEntities(tag.substr(j, end - j));
  }
  return std::string();
}

void appendUtf8(std::string& out, unsigned long cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string firstToken(const std::string& raw) {
  size_t i = 0;
  while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\r' ||
                            raw[i] == '\n'))
    ++i;
  std::string tok;
  while (i < raw.size() && raw[i] != ' ' && raw[i] != '\t' && raw[i] != '\r' &&
         raw[i] != '\n') {
    tok.push_back(raw[i++]);
  }
  return tok;
}

// Element text from `i` up to the case-insensitive closing tag; advances `i`.
std::string readText(const std::string& html, size_t& i, const char* closeTag) {
  size_t pos = findNoCase(html, closeTag, i);
  std::string text;
  if (pos == std::string::npos) {
    text = html.substr(i);
    i = html.size();
  } else {
    text = html.substr(i, pos - i);
    i = pos + std::strlen(closeTag);
  }
  return bmDecodeEntities(text);
}

void walkExport(const std::map<int64_t, std::vector<const BookmarkRow*>>& kids,
                int64_t parent, int depth, std::string& out) {
  auto it = kids.find(parent);
  if (it == kids.end()) return;
  std::string pad(static_cast<size_t>(depth) * 4, ' ');
  for (const BookmarkRow* r : it->second) {
    if (r->url.empty()) {
      out += pad + "<DT><H3>" + escapeHtml(r->name) + "</H3>\n";
      out += pad + "<DL><p>\n";
      walkExport(kids, r->id, depth + 1, out);
      out += pad + "</DL><p>\n";
    } else {
      out += pad + "<DT><A HREF=\"" + escapeHtml(r->url) + "\">" +
             escapeHtml(r->name) + "</A>\n";
    }
  }
}

}  // namespace

std::string bmDecodeEntities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] != '&') {
      out.push_back(s[i++]);
      continue;
    }
    size_t semi = s.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 10) {
      out.push_back(s[i++]);
      continue;
    }
    std::string ent = s.substr(i + 1, semi - i - 1);
    if (ent == "amp") out.push_back('&');
    else if (ent == "lt") out.push_back('<');
    else if (ent == "gt") out.push_back('>');
    else if (ent == "quot") out.push_back('"');
    else if (ent == "apos" || ent == "#39") out.push_back('\'');
    else if (ent == "#x27" || ent == "#X27") out.push_back('\'');
    else if (ent == "nbsp") out.push_back(' ');
    else if (ent.size() > 1 && ent[0] == '#') {
      unsigned long code = 0;
      bool ok = true;
      if (ent[1] == 'x' || ent[1] == 'X') {
        for (size_t k = 2; k < ent.size(); ++k) {
          char c = ent[k];
          int d;
          if (c >= '0' && c <= '9') d = c - '0';
          else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
          else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
          else { ok = false; break; }
          code = code * 16 + static_cast<unsigned long>(d);
          if (code > 0x10FFFF) { ok = false; break; }
        }
      } else {
        for (size_t k = 1; k < ent.size(); ++k) {
          if (ent[k] < '0' || ent[k] > '9') { ok = false; break; }
          code = code * 10 + static_cast<unsigned long>(ent[k] - '0');
          if (code > 0x10FFFF) { ok = false; break; }
        }
      }
      if (ok && code > 0) appendUtf8(out, code);
      else out += s.substr(i, semi - i + 1);
    } else {
      out += s.substr(i, semi - i + 1);
    }
    i = semi + 1;
  }
  return out;
}

std::string exportNetscapeHtml(const std::vector<BookmarkRow>& rows) {
  std::map<int64_t, const BookmarkRow*> byId;
  for (const auto& r : rows) byId[r.id] = &r;
  std::map<int64_t, std::vector<const BookmarkRow*>> kids;
  for (const auto& r : rows) {
    int64_t p = (r.parent != r.id && byId.count(r.parent)) ? r.parent : 0;
    kids[p].push_back(&r);
  }
  std::string out =
      "<!DOCTYPE NETSCAPE-ABookmark-file-1>\n"
      "<!-- This is an automatically generated file.\n"
      "     It will be read and overwritten.\n"
      "     DO NOT EDIT! -->\n"
      "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
      "<TITLE>Bookmarks</TITLE>\n"
      "<H1>Bookmarks</H1>\n"
      "<DL><p>\n";
  walkExport(kids, 0, 1, out);
  out += "</DL><p>\n";
  return out;
}

bool parseNetscapeHtml(const std::string& html, std::vector<BmNode>& out) {
  out.clear();
  BmNode root;
  std::vector<BmNode*> stack;
  stack.push_back(&root);
  const size_t kNone = static_cast<size_t>(-1);
  size_t pendingFolder = kNone;  // index in stack.back()->children
  size_t nodes = 0;
  size_t i = 0;
  const size_t n = html.size();
  while (i < n) {
    size_t lt = html.find('<', i);
    if (lt == std::string::npos) break;
    if (noCaseEq(html, lt, "<!--")) {
      size_t end = html.find("-->", lt + 4);
      i = end == std::string::npos ? n : end + 3;
      continue;
    }
    size_t gt = findTagEnd(html, lt);
    if (gt == std::string::npos) break;
    std::string raw = html.substr(lt + 1, gt - lt - 1);
    i = gt + 1;
    bool closing = false;
    if (!raw.empty() && raw[0] == '/') {
      closing = true;
      raw.erase(0, 1);
    }
    std::string tok = asciiLower(firstToken(raw));
    if (tok == "dl") {
      if (closing) {
        if (stack.size() > 1) {
          stack.pop_back();
          pendingFolder = kNone;
        }
      } else if (pendingFolder != kNone) {
        stack.push_back(&stack.back()->children[pendingFolder]);
        pendingFolder = kNone;
      } else {
        stack.push_back(stack.back());
      }
      continue;
    }
    if (closing) continue;
    if (tok == "h3") {
      std::string name = readText(html, i, "</H3>");
      pendingFolder = kNone;
      if (nodes < kMaxParseNodes) {
        stack.back()->children.push_back(BmNode());
        stack.back()->children.back().name = name.substr(0, kMaxNameLen);
        pendingFolder = stack.back()->children.size() - 1;
        ++nodes;
      }
      continue;
    }
    if (tok == "a") {
      std::string href = attrValue(raw, "href");
      std::string name = readText(html, i, "</A>");
      pendingFolder = kNone;
      if (nodes < kMaxParseNodes && !href.empty()) {
        stack.back()->children.push_back(BmNode());
        BmNode& link = stack.back()->children.back();
        link.name = name.substr(0, kMaxNameLen);
        link.url = href.substr(0, kMaxUrlLen);
        ++nodes;
      }
      continue;
    }
  }
  out = std::move(root.children);
  return !out.empty();
}

}  // namespace web98
