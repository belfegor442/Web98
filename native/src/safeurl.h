#pragma once
// Port of src/shared/safeurl.js — URL policy gates. Behavior must stay
// byte-compatible with the Electron implementation (unit-tested both sides).
#include <string>
#include <vector>

namespace web98 {

constexpr size_t kMaxUrlLen = 8192;

// Lowercased scheme incl. colon; "" for empty/too long/control-char input.
std::string schemeOf(const std::string& url);

// Address-bar / trusted navigation gate.
bool isLoadableUrl(const std::string& url);

// Page-initiated navigation gate (will-navigate / window.open).
// fromWeb: the opener/tab URL is a web scheme (http|https|ftp|blob).
bool isAllowedNavigation(const std::string& url, bool fromWeb);

bool isWebUrl(const std::string& url);

// Only mailto:/tel: may be handed to the OS.
bool isSafeExternalUrl(const std::string& url);

// Bookmarks/favorites may store http/https/ftp/file only.
bool isBookmarkUrl(const std::string& url);

bool isSearchTemplate(const std::string& t);

// Keeps only loadable strings, in order, up to clamped max.
std::vector<std::string> sanitizeSessionList(const std::vector<std::string>& list, int max);

}  // namespace web98
