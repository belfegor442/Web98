#include "session.h"

#include "safeurl.h"

namespace web98 {
namespace {

SessionWindow sanitizeWindow(const SessionWindow& w) {
  SessionWindow out;
  out.urls = sanitizeSessionList(w.urls, 40);
  if (out.urls.empty()) return out;
  out.active = w.active;
  if (out.active < 0 || out.active >= static_cast<int>(out.urls.size())) out.active = 0;
  return out;
}

}  // namespace

std::vector<SessionWindow> parseStoredSession(const Json& stored) {
  std::vector<SessionWindow> out;
  if (!stored.isObject()) return out;
  const Json* windows = stored.find("windows");
  if (windows && windows->isArray()) {
    for (const auto& w : windows->arr) {
      if (static_cast<int>(out.size()) >= 8) break;
      if (!w.isObject()) continue;
      SessionWindow sw;
      const Json* urls = w.find("urls");
      if (urls && urls->isArray()) {
        for (const auto& u : urls->arr)
          if (u.isString()) sw.urls.push_back(u.str);
      }
      const Json* active = w.find("active");
      if (active && active->isNumber()) sw.active = static_cast<int>(active->num);
      SessionWindow clean = sanitizeWindow(sw);
      if (!clean.urls.empty()) out.push_back(std::move(clean));
    }
    return out;
  }
  // Legacy single-window shape {urls, active}
  const Json* urls = stored.find("urls");
  if (urls && urls->isArray()) {
    SessionWindow sw;
    for (const auto& u : urls->arr)
      if (u.isString()) sw.urls.push_back(u.str);
    const Json* active = stored.find("active");
    if (active && active->isNumber()) sw.active = static_cast<int>(active->num);
    SessionWindow clean = sanitizeWindow(sw);
    if (!clean.urls.empty()) out.push_back(std::move(clean));
  }
  return out;
}

Json buildStoredSession(const std::vector<SessionWindow>& windows) {
  Json arr = Json::makeArray();
  for (const auto& w : windows) {
    SessionWindow clean = sanitizeWindow(w);
    if (clean.urls.empty()) continue;
    Json entry = Json::makeObject();
    Json urls = Json::makeArray();
    for (const auto& u : clean.urls) urls.arr.push_back(Json::makeString(u));
    entry.obj.emplace_back("urls", std::move(urls));
    entry.obj.emplace_back("active", Json::makeNumber(clean.active));
    arr.arr.push_back(std::move(entry));
    if (arr.arr.size() >= 8) break;
  }
  if (arr.arr.empty()) return Json::makeNull();
  Json out = Json::makeObject();
  out.obj.emplace_back("windows", std::move(arr));
  return out;
}

}  // namespace web98
