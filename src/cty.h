#pragma once

// Terraform's value model ("cty") as far as this provider needs it, and its two wire encodings:
// the msgpack that Core sends in a DynamicValue, and the JSON that Core stores a resource's state in.
//
// The encoding follows go-cty v1.19.0 (cty/msgpack), which is what OpenTofu uses:
//   null       -> nil
//   unknown    -> the msgpack extension 0xd4 0x00 0x00 (on input, any extension counts as unknown)
//   string     -> str;  bool -> bool
//   number     -> the smallest int if it is a whole number, otherwise a float (go-cty always writes float64;
//                 here a value that float32 holds exactly is written as float32. Core reads both the same way.)
//                 On input also uint, or a string holding the number.
//   list, set  -> array;  map -> map;  object -> map holding EVERY attribute, sorted by name
// A value always travels together with its type: nothing on the wire says what type it has.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"

namespace cty {

class Type;
using TypePtr = std::shared_ptr<const Type>;

// Only the types this provider uses. (Tuples and the dynamic pseudo-type are not supported.)
class Type {
 public:
  enum class Kind { kString, kNumber, kBool, kList, kSet, kMap, kObject };

  static TypePtr String();
  static TypePtr Number();
  static TypePtr Bool();
  static TypePtr List(TypePtr element);
  static TypePtr Set(TypePtr element);
  static TypePtr Map(TypePtr element);
  static TypePtr Object(std::map<std::string, TypePtr> attributes);

  Kind kind() const { return kind_; }
  const TypePtr& element() const { return element_; }                        // list, set, map
  const std::map<std::string, TypePtr>& attributes() const { return attributes_; }  // object

  // The JSON that a schema carries for an attribute's type: "string", ["list","string"],
  // ["object",{"a":"number"}], ...
  std::string ToJson() const;

 private:
  explicit Type(Kind kind) : kind_(kind) {}
  Kind kind_;
  TypePtr element_;
  std::map<std::string, TypePtr> attributes_;
};

class Value {
 public:
  enum class Kind { kNull, kUnknown, kString, kNumber, kBool, kList, kSet, kMap, kObject };

  Value() = default;  // null
  static Value Null() { return Value(); }
  static Value Unknown();
  static Value String(std::string value);
  static Value Bool(bool value);
  static Value Number(int64_t value);
  static Value Number(double value);  // a whole number is stored exactly like the int64 one
  static Value List(std::vector<Value> elements);
  static Value Set(std::vector<Value> elements);  // sorted, duplicates removed, like a cty set
  static Value Map(std::map<std::string, Value> entries);
  static Value Object(std::map<std::string, Value> attributes);

  Kind kind() const { return kind_; }
  bool IsNull() const { return kind_ == Kind::kNull; }
  bool IsUnknown() const { return kind_ == Kind::kUnknown; }
  bool IsKnown() const { return kind_ != Kind::kUnknown; }
  bool IsWhollyKnown() const;  // no unknown anywhere inside

  const std::string& AsString() const { return string_; }
  bool AsBool() const { return bool_; }
  bool IsInteger() const { return integer_; }  // a number that is an exact int64
  int64_t AsInt64() const { return integer_value_; }
  double AsDouble() const { return integer_ ? static_cast<double>(integer_value_) : double_; }
  const std::vector<Value>& Elements() const { return elements_; }            // list, set
  const std::map<std::string, Value>& Entries() const { return entries_; }    // map, object

  // An attribute of an object value; null if there is no such attribute.
  const Value& Attr(const std::string& name) const;

  bool operator==(const Value& other) const;  // structural; unknown equals unknown
  bool operator!=(const Value& other) const { return !(*this == other); }

  std::string DebugString() const;  // for test failures and logs, not a wire format

 private:
  static int Compare(const Value& a, const Value& b);

  Kind kind_ = Kind::kNull;
  std::string string_;
  bool bool_ = false;
  bool integer_ = false;
  int64_t integer_value_ = 0;
  double double_ = 0;
  std::vector<Value> elements_;
  std::map<std::string, Value> entries_;
};

// The two directions of the msgpack encoding. Errors say where in the value the problem is, e.g.
// `.tags["env"]: a string is required`.
absl::StatusOr<std::string> EncodeMsgpack(const Value& value, const Type& type);
absl::StatusOr<Value> DecodeMsgpack(std::string_view bytes, const Type& type);

// Core stores a resource's state as JSON; UpgradeResourceState hands it back to us in that form.
// No unknown values can occur here.
absl::StatusOr<Value> DecodeJson(std::string_view json, const Type& type);

}  // namespace cty
