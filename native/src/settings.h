#pragma once
// Settings model: defaults, load-time validation and the renderer-facing
// patch whitelist. Ports src/main/settings.js + src/shared/validate.js.
#include <cstdint>
#include <string>
#include <vector>

#include "json.h"

namespace web98 {

struct EngineDef {
  std::string name;
  std::string tmpl;
};

std::vector<EngineDef> builtinEngines();
std::string defaultSearchTemplate();  // DuckDuckGo fallback literal

// --- pure helpers (unit-tested) --------------------------------------------

// Strip control chars, trim, cap length (JS sanitizeText).
std::string sanitizeText(const std::string& v, size_t max);
bool isGrantOrigin(const std::string& origin);
bool isPermissionName(const std::string& p);


// Validate one incoming patch value (settings:set whitelist). Returns true
// when the key may be written; *value holds the sanitized replacement
// (e.g. clamped zoom). Unknown/forbidden keys -> false + rejected.
bool sanitizeSettingsPatchValue(const std::string& key, const Json& in, Json& out,
                                std::vector<std::string>* rejected);

// On-load validation of a raw stored value. Returns the value to cache.
Json validateStoredValue(const std::string& key, const Json& raw);

// engines value sanitization (builtins first, extras appended).
Json sanitizeEngines(const Json& v);

// permissions object sanitization ({origin:{perm:bool}}).
Json sanitizePermissions(const Json& v);

// Effective search template for a full settings cache.
std::string searchTemplateFor(const Json& settings);

// Clamp helper matching clampNumber(v,min,max,dflt).
double clampNumber(double v, double lo, double hi, double dflt);

// --- validate.js ports used by the IPC dispatcher --------------------------

// Number.isInteger(Number(v)) && v >= 0. Returns false for anything else.
bool sanitizeId(const Json& v, int64_t& out);

// One bookmark/folder entry after validation.
struct BookmarkIn {
  std::string name;
  std::string url;   // "" = folder
  int64_t parent = 0;
};

// sanitizeBookmark: null-safe single entry (false when unusable).
bool sanitizeBookmark(const Json& raw, BookmarkIn& out);
// sanitizeBookmarkList: bad rows skipped, capped at maxRows.
std::vector<BookmarkIn> sanitizeBookmarkList(const Json& raw, int maxRows = 5000);

// --- model -----------------------------------------------------------------

class SettingsModel {
 public:
  // Loads defaults + rows already persisted in `getSetting` callbacks.
  // The owner wires this to Web98Storage.
  void initEmpty();  // defaults only (fresh profile / unit tests)

  const Json& all() const { return cache_; }
  Json get(const std::string& key) const;
  std::string getString(const std::string& key, const std::string& dflt) const;
  bool getBool(const std::string& key, bool dflt) const;
  double getNumber(const std::string& key, double dflt) const;

  // Apply a patch (already sanitized per-key by caller or via
  // sanitizeSettingsPatchValue). Returns rejected keys.
  std::vector<std::string> applyPatch(const Json& patch);

  void setRaw(const std::string& key, const Json& value);  // load path

  // Mutators used internally; return true when the value changed.
  bool setString(const std::string& key, const std::string& value);
  bool setBool(const std::string& key, bool value);
  bool setNumber(const std::string& key, double value);

 private:
  Json cache_ = Json::makeObject();
};

}  // namespace web98
