#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <regex>

#include "base.h"
#include "safeurl.h"

namespace web98 {
namespace {

bool isPlainObject(const Json& v) { return v.isObject(); }

bool truthy(const Json& v) {  // JS !!x
  switch (v.type) {
    case Json::Null: return false;
    case Json::Bool: return v.b;
    case Json::Number: return v.num != 0 && !std::isnan(v.num);
    case Json::String: return !v.str.empty();
    default: return true;
  }
}

bool finiteNumber(const Json& v, double& out) {
  if (v.isNumber()) {
    out = v.num;
    return std::isfinite(out);
  }
  if (v.isString() && !v.str.empty()) {
    char* end = nullptr;
    std::string s = trim(v.str);
    double d = std::strtod(s.c_str(), &end);
    if (end && *end == '\0' && std::isfinite(d)) {
      out = d;
      return true;
    }
  }
  return false;
}

bool isFiniteJs(const Json& v) {  // Number.isFinite(Number(v))
  double d;
  if (v.isNumber()) return std::isfinite(v.num);
  if (v.isNull()) return false;
  if (v.isBool()) return true;  // Number(true)=1 finite, Number(false)=0
  if (v.isString()) {
    std::string s = trim(v.str);
    if (s.empty()) return false;
    char* end = nullptr;
    double d2 = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && std::isfinite(d2);
  }
  (void)d;
  return false;
}

Json defaultEnginesJson() {
  Json arr = Json::makeArray();
  for (const auto& e : builtinEngines()) {
    Json o = Json::makeObject();
    o.obj.emplace_back("name", Json::makeString(e.name));
    o.obj.emplace_back("template", Json::makeString(e.tmpl));
    arr.arr.push_back(std::move(o));
  }
  return arr;
}

}  // namespace

std::vector<EngineDef> builtinEngines() {
  return {
      {"DuckDuckGo", "https://duckduckgo.com/?q=%s"},
      {"Google", "https://www.google.com/search?q=%s"},
      {"Bing", "https://www.bing.com/search?q=%s"},
      {"Wikipedia", "https://en.wikipedia.org/w/index.php?search=%s"},
  };
}

std::string defaultSearchTemplate() { return "https://duckduckgo.com/?q=%s"; }

double clampNumber(double v, double lo, double hi, double dflt) {
  if (!std::isfinite(v)) return dflt;
  return std::min(hi, std::max(lo, v));
}

std::string sanitizeText(const std::string& v, size_t max) {
  std::string s;
  for (char c : v) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x01 && u <= 0x1f) continue;
    if (u == 0x7f) continue;
    s += c;
  }
  s = trim(s);
  if (s.size() > max) s = s.substr(0, max);
  return s;
}

bool isGrantOrigin(const std::string& origin) {
  if (origin == "null") return true;
  static const std::regex re("^https?://[^\\s]{1,300}$");
  return std::regex_match(origin, re);
}

bool isPermissionName(const std::string& p) {
  static const std::regex re("^[a-z][a-z0-9\\-]{0,63}$", std::regex_constants::icase);
  return std::regex_match(p, re);
}

bool sanitizeId(const Json& v, int64_t& out) {
  double d = 0;
  if (v.isNumber()) {
    d = v.num;
  } else if (v.isString()) {
    const std::string& s = v.str;
    if (s.empty()) return false;
    char* end = nullptr;
    d = std::strtod(s.c_str(), &end);
    if (!end || *end != '\0') return false;
  } else {
    return false;
  }
  if (!std::isfinite(d) || d < 0 || d != std::floor(d)) return false;
  out = static_cast<int64_t>(d);
  return true;
}

bool sanitizeBookmark(const Json& raw, BookmarkIn& out) {
  if (!raw.isObject()) return false;
  BookmarkIn b;
  const Json* u = raw.find("url");
  if (u && u->isString()) b.url = trim(u->str);
  // non-empty urls must be bookmarkable; empty = folder
  if (!b.url.empty() && !isBookmarkUrl(b.url)) return false;
  const Json* p = raw.find("parent");
  int64_t parent = 0;
  if (p) sanitizeId(*p, parent);
  b.parent = parent;
  const Json* n = raw.find("name");
  b.name = sanitizeText(n && n->isString() ? n->str : std::string(), 200);
  if (b.name.empty()) b.name = b.url.empty() ? std::string("Favorite") : b.url;
  out = std::move(b);
  return true;
}

std::vector<BookmarkIn> sanitizeBookmarkList(const Json& raw, int maxRows) {
  std::vector<BookmarkIn> out;
  if (!raw.isArray()) return out;
  int limit = std::max(1, std::min(maxRows, 20000));
  for (size_t i = 0; i < raw.arr.size() &&
                      static_cast<int>(out.size()) < limit;
       ++i) {
    BookmarkIn b;
    if (sanitizeBookmark(raw.arr[i], b)) out.push_back(std::move(b));
  }
  return out;
}

Json sanitizeEngines(const Json& v) {
  if (!v.isArray()) return Json();  // null
  Json out = Json::makeArray();
  for (size_t i = 0; i < v.arr.size() && out.arr.size() < 16; ++i) {
    const Json& e = v.arr[i];
    if (!isPlainObject(e)) continue;
    Json nameRaw = Json();
    const Json* n = e.find("name");
    if (n) nameRaw = *n;
    std::string name = sanitizeText(n && n->isString() ? n->str : std::string(), 50);
    const Json* t = e.find("template");
    std::string tmpl = t && t->isString() ? t->str : std::string();
    if (name.empty() || !isSearchTemplate(tmpl)) continue;
    Json row = Json::makeObject();
    row.obj.emplace_back("name", Json::makeString(name));
    row.obj.emplace_back("template", Json::makeString(tmpl));
    out.arr.push_back(std::move(row));
    (void)nameRaw;
  }
  return out;
}

Json sanitizePermissions(const Json& v) {
  Json out = Json::makeObject();
  if (!v.isObject()) return out;
  for (const auto& kv : v.obj) {
    if (!isGrantOrigin(kv.first)) continue;
    if (!kv.second.isObject()) continue;
    for (const auto& pv : kv.second.obj) {
      if (!isPermissionName(pv.first)) continue;
      if (!pv.second.isBool()) continue;
      Json* originObj = nullptr;
      for (auto& o : out.obj) {
        if (o.first == kv.first) {
          originObj = &o.second;
          break;
        }
      }
      if (!originObj) {
        out.obj.emplace_back(kv.first, Json::makeObject());
        originObj = &out.obj.back().second;
      }
      originObj->obj.emplace_back(pv.first, Json::makeBool(pv.second.b));
    }
  }
  return out;
}

bool sanitizeSettingsPatchValue(const std::string& key, const Json& in, Json& out,
                                std::vector<std::string>* rejected) {
  auto reject = [&]() {
    if (rejected) rejected->push_back(key);
    return false;
  };
  if (key == "homepage") {
    if (in.isString() && isLoadableUrl(in.str)) {
      out = in;
      return true;
    }
    return reject();
  }
  if (key == "searchEngine") {
    if (in.isString() && in.str.size() <= 50) {
      std::string t = trim(in.str);
      if (!t.empty()) {
        out = Json::makeString(t);
        return true;
      }
    }
    return reject();
  }
  if (key == "engines") {
    Json list = sanitizeEngines(in);
    if (list.isArray() && !list.arr.empty()) {
      out = std::move(list);
      return true;
    }
    if (in.isArray()) return reject();
    return false;  // non-array silently ignored (JS: no out, no rejected)
  }
  if (key == "sessionRestore" || key == "statusBar" || key == "toolbarVisible") {
    if (in.isBool()) {
      out = in;
      return true;
    }
    return reject();
  }
  if (key == "zoom") {
    double n = 1;
    if (in.isNumber())
      n = in.num;
    else if (in.isString() && !trim(in.str).empty()) {
      char* end = nullptr;
      n = std::strtod(trim(in.str).c_str(), &end);
      if (!end || *end != '\0') n = 1;
    } else if (!in.isNumber()) {
      n = 1;
    }
    out = Json::makeNumber(clampNumber(n, 0.25, 3.0, 1.0));
    if (!isFiniteJs(in)) {
      if (rejected) rejected->push_back(key);
      // JS still writes the clamped default
      return true;
    }
    return true;
  }
  if (key == "downloadsDir") {
    if (in.isNull()) {
      out = Json::makeNull();
      return true;
    }
    if (in.isString() && in.str.size() <= 512 && in.str.find('\0') == std::string::npos &&
        isAbsolutePathNative(in.str)) {
      out = in;
      return true;
    }
    return reject();
  }
  return reject();  // unknown key (permissions, lastSession, ...)
}

Json validateStoredValue(const std::string& key, const Json& raw) {
  if (key == "permissions") return sanitizePermissions(raw);
  if (key == "lastSession") return raw;
  if (key == "homepage") {
    if (raw.isString() && isLoadableUrl(raw.str)) return raw;
    return Json();
  }
  if (key == "engines") return Json();  // merged by caller
  if (key == "zoom") {
    double n;
    double v = finiteNumber(raw, n) ? n : 1;
    return Json::makeNumber(clampNumber(v, 0.25, 3.0, 1.0));
  }
  if (key == "searchEngine") {
    if (raw.isString() && !raw.str.empty()) return raw;
    return Json();
  }
  if (key == "downloadsDir") {
    if (raw.isNull()) return Json::makeNull();
    if (raw.isString() && isAbsolutePathNative(raw.str)) return raw;
    return Json::makeNull();
  }
  return raw;
}

std::string searchTemplateFor(const Json& settings) {
  const Json* engines = settings.find("engines");
  const Json* engineSel = settings.find("searchEngine");
  std::string sel = engineSel && engineSel->isString() ? engineSel->str : std::string();
  std::string tmpl;
  if (engines && engines->isArray()) {
    for (const auto& e : engines->arr) {
      const Json* n = e.find("name");
      if (n && n->isString() && n->str == sel) {
        const Json* t = e.find("template");
        if (t && t->isString()) tmpl = t->str;
        break;
      }
    }
    if (!isSearchTemplate(tmpl) && !engines->arr.empty()) {
      const Json* t = engines->arr[0].find("template");
      if (t && t->isString()) tmpl = t->str;
    }
  }
  if (!isSearchTemplate(tmpl)) tmpl = defaultSearchTemplate();
  return tmpl;
}

void SettingsModel::initEmpty() {
  cache_ = Json::makeObject();
  cache_.obj.emplace_back("homepage", Json::makeString(homepageUrl()));
  cache_.obj.emplace_back("searchEngine", Json::makeString("DuckDuckGo"));
  cache_.obj.emplace_back("engines", defaultEnginesJson());
  cache_.obj.emplace_back("sessionRestore", Json::makeBool(true));
  cache_.obj.emplace_back("statusBar", Json::makeBool(true));
  cache_.obj.emplace_back("toolbarVisible", Json::makeBool(true));
  cache_.obj.emplace_back("zoom", Json::makeNumber(1));
  cache_.obj.emplace_back("downloadsDir", Json::makeNull());
  cache_.obj.emplace_back("permissions", Json::makeObject());
  cache_.obj.emplace_back("lastSession", Json::makeNull());
}

void SettingsModel::setRaw(const std::string& key, const Json& value) {
  Json v = validateStoredValue(key, value);
  bool homepageInvalid = (key == "homepage" && v.isNull());
  bool enginesKey = (key == "engines");
  bool searchInvalid = (key == "searchEngine" && v.isNull());
  bool downloadsInvalid = (key == "downloadsDir" && v.isNull() && !value.isNull());
  if (homepageInvalid || searchInvalid || downloadsInvalid) return;  // keep default
  if (enginesKey) {
    // builtins first, sanitized user extras appended
    Json extras = sanitizeEngines(value);
    Json merged = defaultEnginesJson();
    std::vector<std::string> baseNames;
    for (const auto& e : builtinEngines()) baseNames.push_back(e.name);
    if (extras.isArray()) {
      for (auto& e : extras.arr) {
        const Json* n = e.find("name");
        bool isBase = false;
        if (n && n->isString()) {
          for (const auto& bn : baseNames)
            if (bn == n->str) isBase = true;
        }
        if (!isBase) merged.arr.push_back(e);
      }
    }
    for (auto& kv : cache_.obj) {
      if (kv.first == "engines") {
        kv.second = merged;
        return;
      }
    }
    cache_.obj.emplace_back("engines", std::move(merged));
    return;
  }
  for (auto& kv : cache_.obj) {
    if (kv.first == key) {
      kv.second = std::move(v);
      return;
    }
  }
  cache_.obj.emplace_back(key, std::move(v));
}

Json SettingsModel::get(const std::string& key) const {
  const Json* v = cache_.find(key);
  return v ? *v : Json();
}

std::string SettingsModel::getString(const std::string& key, const std::string& dflt) const {
  const Json* v = cache_.find(key);
  return v && v->isString() ? v->str : dflt;
}

bool SettingsModel::getBool(const std::string& key, bool dflt) const {
  const Json* v = cache_.find(key);
  return v && v->isBool() ? v->b : dflt;
}

double SettingsModel::getNumber(const std::string& key, double dflt) const {
  const Json* v = cache_.find(key);
  return v && v->isNumber() ? v->num : dflt;
}

std::vector<std::string> SettingsModel::applyPatch(const Json& patch) {
  std::vector<std::string> rejected;
  if (!patch.isObject()) return rejected;
  for (const auto& kv : patch.obj) {
    Json out;
    if (sanitizeSettingsPatchValue(kv.first, kv.second, out, &rejected)) {
      setRaw(kv.first, out);
      // setRaw validates stored values; patch values are already valid.
      for (auto& c : cache_.obj) {
        if (c.first == kv.first) {
          c.second = out;
          break;
        }
      }
    }
  }
  return rejected;
}

bool SettingsModel::setString(const std::string& key, const std::string& value) {
  Json cur = get(key);
  if (cur.isString() && cur.str == value) return false;
  Json v = Json::makeString(value);
  setRaw(key, v);
  for (auto& c : cache_.obj) {
    if (c.first == key) {
      c.second = v;
      break;
    }
  }
  return true;
}

bool SettingsModel::setBool(const std::string& key, bool value) {
  Json cur = get(key);
  if (cur.isBool() && cur.b == value) return false;
  Json v = Json::makeBool(value);
  for (auto& c : cache_.obj) {
    if (c.first == key) {
      c.second = v;
      break;
    }
  }
  if (!cur.isBool()) {
    for (auto& c : cache_.obj) {
      if (c.first == key) return true;
    }
    cache_.obj.emplace_back(key, v);
  }
  return true;
}

bool SettingsModel::setNumber(const std::string& key, double value) {
  Json cur = get(key);
  if (cur.isNumber() && cur.num == value) return false;
  Json v = Json::makeNumber(value);
  for (auto& c : cache_.obj) {
    if (c.first == key) {
      c.second = v;
      break;
    }
  }
  return true;
}

}  // namespace web98
