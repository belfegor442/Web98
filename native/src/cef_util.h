#pragma once
// Small CEF helpers shared by the app, client and engine layers.
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "include/base/cef_macros.h"
#include "include/cef_string_visitor.h"
#include "include/cef_task.h"

namespace web98 {

// CefString -> std::string convenience.
inline std::string str(const CefString& s) { return s.ToString(); }

// CefPostTask only accepts CefRefPtr<CefTask>; this adapts std::function.
class UiTask : public CefTask {
 public:
  explicit UiTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override {
    if (fn_) fn_();
  }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(UiTask);
  DISALLOW_COPY_AND_ASSIGN(UiTask);
};

// CefStringVisitor (GetSource/GetText) -> std::function.
class StringSink : public CefStringVisitor {
 public:
  explicit StringSink(std::function<void(const CefString&)> fn)
      : fn_(std::move(fn)) {}
  void Visit(const CefString& str) override {
    if (fn_) fn_(str);
  }

 private:
  std::function<void(const CefString&)> fn_;
  IMPLEMENT_REFCOUNTING(StringSink);
  DISALLOW_COPY_AND_ASSIGN(StringSink);
};

inline bool PostUi(std::function<void()> fn) {
  return CefPostTask(TID_UI, new UiTask(std::move(fn)));
}

inline bool PostUiDelayed(std::function<void()> fn, int64_t delayMs) {
  return CefPostDelayedTask(TID_UI, new UiTask(std::move(fn)), delayMs);
}

// Quote `s` as a JavaScript string literal (UTF-8 in, escaped quotes).
inline std::string jsQuote(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else if (c == 0xE2 && i + 2 < s.size() &&
                   static_cast<unsigned char>(s[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(s[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(s[i + 2]) == 0xA9)) {
          // U+2028 / U+2029 in UTF-8 -> escape (legal JSON, historic JS pitfall)
          out += (static_cast<unsigned char>(s[i + 2]) == 0xA8) ? "\\u2028"
                                                               : "\\u2029";
          i += 2;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}

}  // namespace web98
