#include "security.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include "base.h"
#include "settings.h"  // isPermissionName/isGrantOrigin

namespace web98 {
namespace {
constexpr int64_t kCertGrantTtlMs = 5 * 60 * 1000;

bool certErrorDesc(const std::string& desc) {
  // Only certificate errors get the "Continue anyway" affordance. SSL
  // protocol errors (ERR_SSL_VERSION_OR_CIPHER_MISMATCH etc.) are not
  // bypassable and must stay classified as generic network failures.
  static const std::regex re("ERR_CERT", std::regex_constants::icase);
  return std::regex_search(desc, re);
}
}  // namespace

void CertGrants::grant(const std::string& url) {
  if (url.empty()) return;
  grants_[url] = nowMs() + kCertGrantTtlMs;
}

bool CertGrants::allows(const std::string& url) const {
  int64_t now = nowMs();
  for (auto it = grants_.begin(); it != grants_.end();) {
    if (it->second <= now)
      it = grants_.erase(it);
    else
      ++it;
  }
  auto it = grants_.find(url);
  return it != grants_.end() && it->second > now;
}

std::string classifyLoadError(int code, const std::string& desc) {
  if (certErrorDesc(desc)) return "cert";
  if (code <= -200 && code >= -299) return "cert";
  return "net";
}

bool permissionAllowed(const Json& permissions, const std::string& origin,
                       const std::string& permission) {
  if (!isPermissionName(permission)) return false;
  if (permissions.isObject()) {
    const Json* byOrigin = permissions.find(origin);
    if (byOrigin && byOrigin->isObject()) {
      const Json* v = byOrigin->find(permission);
      if (v && v->isBool()) return v->b;
    }
  }
  if (permission == "fullscreen") return true;
  return false;
}

}  // namespace web98
