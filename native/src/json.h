#pragma once
// Minimal dependency-free JSON value + parser/serializer (Web98 settings,
// session and engines round-trip). Compact dump matches JSON.stringify's
// default output; parse accepts any valid JSON.
#include <string>
#include <vector>
#include <utility>

namespace web98 {

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object };
  Type type = Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;

  Json() = default;
  static Json makeNull() { return Json(); }
  static Json makeBool(bool v) { Json j; j.type = Bool; j.b = v; return j; }
  static Json makeNumber(double v) { Json j; j.type = Number; j.num = v; return j; }
  static Json makeString(std::string v) { Json j; j.type = String; j.str = std::move(v); return j; }
  static Json makeArray() { Json j; j.type = Array; return j; }
  static Json makeObject() { Json j; j.type = Object; return j; }

  bool isNull() const { return type == Null; }
  bool isBool() const { return type == Bool; }
  bool isNumber() const { return type == Number; }
  bool isString() const { return type == String; }
  bool isArray() const { return type == Array; }
  bool isObject() const { return type == Object; }

  // Object lookup; nullptr when absent or not an object.
  const Json* find(const std::string& key) const;
  // Convenience accessors with defaults.
  bool boolOr(bool dflt) const { return type == Bool ? b : dflt; }
  double numberOr(double dflt) const { return type == Number ? num : dflt; }
  std::string stringOr(const std::string& dflt) const { return type == String ? str : dflt; }

  static bool parse(const std::string& text, Json& out);
  static bool parseStrict(const std::string& text, Json& out) { return parse(text, out); }
  std::string dump() const;
};

}  // namespace web98
