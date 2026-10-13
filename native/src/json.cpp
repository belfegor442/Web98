#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace web98 {
namespace {

struct Parser {
  const char* p;
  const char* end;
  int depth = 0;
  static const int kMaxDepth = 256;

  bool ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p < end;
  }
  bool lit(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    if (static_cast<size_t>(end - p) < n) return false;
    for (size_t i = 0; i < n; ++i)
      if (p[i] != s[i]) return false;
    p += n;
    return true;
  }
  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  bool hex4(unsigned& v) {
    if (end - p < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = p[i];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return false;
    }
    p += 4;
    return true;
  }
  bool string(std::string& out) {
    if (p >= end || *p != '"') return false;
    ++p;
    out.clear();
    while (p < end) {
      char c = *p++;
      if (c == '"') return true;
      if (c == '\\') {
        if (p >= end) return false;
        char e = *p++;
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            unsigned cp;
            if (!hex4(cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair
              if (end - p >= 2 && p[0] == '\\' && p[1] == 'u') {
                p += 2;
                unsigned lo;
                if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              } else {
                return false;
              }
            }
            appendUtf8(out, cp);
            break;
          }
          default: return false;
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        return false;
      } else {
        out += c;
      }
    }
    return false;
  }
  bool value(Json& out) {
    if (++depth > kMaxDepth) return false;
    if (!ws()) { --depth; return false; }
    bool ok = false;
    char c = *p;
    if (c == '{') ok = object(out);
    else if (c == '[') ok = array(out);
    else if (c == '"') { out = Json::makeString(""); ok = string(out.str); }
    else if (c == 't') { ok = lit("true"); if (ok) out = Json::makeBool(true); }
    else if (c == 'f') { ok = lit("false"); if (ok) out = Json::makeBool(false); }
    else if (c == 'n') { ok = lit("null"); if (ok) out = Json::makeNull(); }
    else ok = number(out);
    --depth;
    return ok;
  }
  bool object(Json& out) {
    ++p;  // '{'
    out = Json::makeObject();
    if (!ws()) return false;
    if (*p == '}') { ++p; return true; }
    for (;;) {
      if (!ws()) return false;
      std::string key;
      if (!string(key)) return false;
      if (!ws() || *p != ':') return false;
      ++p;
      Json v;
      if (!value(v)) return false;
      out.obj.emplace_back(std::move(key), std::move(v));
      if (!ws()) return false;
      if (*p == ',') { ++p; continue; }
      if (*p == '}') { ++p; return true; }
      return false;
    }
  }
  bool array(Json& out) {
    ++p;  // '['
    out = Json::makeArray();
    if (!ws()) return false;
    if (*p == ']') { ++p; return true; }
    for (;;) {
      Json v;
      if (!value(v)) return false;
      out.arr.push_back(std::move(v));
      if (!ws()) return false;
      if (*p == ',') { ++p; continue; }
      if (*p == ']') { ++p; return true; }
      return false;
    }
  }
  bool number(Json& out) {
    const char* start = p;
    if (p < end && *p == '-') ++p;
    if (p >= end || !(*p >= '0' && *p <= '9')) return false;
    while (p < end && *p >= '0' && *p <= '9') ++p;
    if (p < end && *p == '.') {
      ++p;
      if (p >= end || !(*p >= '0' && *p <= '9')) return false;
      while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
      ++p;
      if (p < end && (*p == '+' || *p == '-')) ++p;
      if (p >= end || !(*p >= '0' && *p <= '9')) return false;
      while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    double parsed = std::strtod(std::string(start, p).c_str(), nullptr);
    if (!std::isfinite(parsed)) return false;
    out = Json::makeNumber(parsed);
    return true;
  }
};

void dumpString(const std::string& s, std::string& out) {
  out += '"';
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void dumpNumber(double v, std::string& out) {
  if (!std::isfinite(v)) { out += "null"; return; }
  if (std::fabs(v) < 1e15 && std::trunc(v) == v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    out += buf;
    return;
  }
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  // Trim to shortest round-trippable representation like JS does.
  double back = std::strtod(buf, nullptr);
  if (back == v) {
    for (char* q = buf; *q; ++q) {
      if (*q == 'g' || *q == 'G') { /* scientific: keep */ }
    }
    std::string s(buf);
    if (s.find('e') == std::string::npos && s.find('.') != std::string::npos) {
      while (!s.empty() && s.back() == '0') s.pop_back();
      if (!s.empty() && s.back() == '.') s.pop_back();
    }
    out += s;
  } else {
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    out += buf;
  }
}

void dumpValue(const Json& j, std::string& out) {
  switch (j.type) {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += (j.b ? "true" : "false"); break;
    case Json::Number: dumpNumber(j.num, out); break;
    case Json::String: dumpString(j.str, out); break;
    case Json::Array: {
      out += '[';
      for (size_t i = 0; i < j.arr.size(); ++i) {
        if (i) out += ',';
        dumpValue(j.arr[i], out);
      }
      out += ']';
      break;
    }
    case Json::Object: {
      out += '{';
      bool first = true;
      for (const auto& kv : j.obj) {
        if (!first) out += ',';
        first = false;
        dumpString(kv.first, out);
        out += ':';
        dumpValue(kv.second, out);
      }
      out += '}';
      break;
    }
  }
}

}  // namespace

const Json* Json::find(const std::string& key) const {
  if (type != Object) return nullptr;
  for (const auto& kv : obj)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

bool Json::parse(const std::string& text, Json& out) {
  Parser ps{text.data(), text.data() + text.size()};
  if (!ps.ws()) return false;
  if (!ps.value(out)) return false;
  ps.ws();  // skip trailing whitespace (may land on end)
  return ps.p == ps.end;
}

std::string Json::dump() const {
  std::string out;
  dumpValue(*this, out);
  return out;
}

}  // namespace web98
