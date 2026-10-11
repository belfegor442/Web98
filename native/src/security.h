#pragma once
// Navigation/permission/certificate policy (CEF-independent logic).
// Electron parity: security.js + permissionDecision + cert grants.
#include <map>
#include <string>

#include "json.h"

namespace web98 {

// --- certificate grants -----------------------------------------------------
// "Continue anyway" decisions apply per URL for 5 minutes (re-click renews).
class CertGrants {
 public:
  void grant(const std::string& url);         // renew
  bool allows(const std::string& url) const;  // prunes expired entries
  void clear() { grants_.clear(); }

 private:
  mutable std::map<std::string, int64_t> grants_;  // url -> expiry ms
};

// ERR_CERT/ERR_SSL range classification used by the error page.
// Returns "cert" or "net".
std::string classifyLoadError(int code, const std::string& desc);

// --- permissions ------------------------------------------------------------
// Stored shape: {origin:{permission:bool}}. Default: fullscreen allowed,
// everything else denied; stored grants win.
bool permissionAllowed(const Json& permissions, const std::string& origin,
                       const std::string& permission);

}  // namespace web98
