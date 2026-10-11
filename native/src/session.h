#pragma once
// Multi-window session persistence (settings key "lastSession").
// Shape: {windows:[{urls:[...],active:n},...]} (legacy: {urls,active}).
#include <string>
#include <vector>

#include "json.h"

namespace web98 {

struct SessionWindow {
  std::vector<std::string> urls;
  int active = 0;
};

// Read a stored lastSession value (multi-window or legacy single-window).
// Filters to loadable urls (<=40 per window, <=8 windows, non-empty).
std::vector<SessionWindow> parseStoredSession(const Json& stored);

// Serialize live windows; skips windows whose url list ends up empty.
// Returns null when nothing usable (caller must then keep previous value).
Json buildStoredSession(const std::vector<SessionWindow>& windows);

}  // namespace web98
