#include "cty.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

namespace cty {

namespace {

using Json = nlohmann::json;

const Value& NullValue() {
  static const Value kNull;
  return kNull;
}

// Largest whole number a double holds exactly; a whole double up to here is stored as an int64.
constexpr double kMaxExactInteger = 9007199254740992.0;  // 2^53

}  // namespace

// --- Type ------------------------------------------------------------------------------------------------------------

TypePtr Type::String() {
  static const TypePtr kType(new Type(Kind::kString));
  return kType;
}
TypePtr Type::Number() {
  static const TypePtr kType(new Type(Kind::kNumber));
  return kType;
}
TypePtr Type::Bool() {
  static const TypePtr kType(new Type(Kind::kBool));
  return kType;
}
TypePtr Type::List(TypePtr element) {
  auto* type = new Type(Kind::kList);
  type->element_ = std::move(element);
  return TypePtr(type);
}
TypePtr Type::Set(TypePtr element) {
  auto* type = new Type(Kind::kSet);
  type->element_ = std::move(element);
  return TypePtr(type);
}
TypePtr Type::Map(TypePtr element) {
  auto* type = new Type(Kind::kMap);
  type->element_ = std::move(element);
  return TypePtr(type);
}
TypePtr Type::Object(std::map<std::string, TypePtr> attributes) {
  auto* type = new Type(Kind::kObject);
  type->attributes_ = std::move(attributes);
  return TypePtr(type);
}

namespace {

Json TypeToJson(const Type& type) {
  switch (type.kind()) {
    case Type::Kind::kString:
      return "string";
    case Type::Kind::kNumber:
      return "number";
    case Type::Kind::kBool:
      return "bool";
    case Type::Kind::kList:
      return Json::array({"list", TypeToJson(*type.element())});
    case Type::Kind::kSet:
      return Json::array({"set", TypeToJson(*type.element())});
    case Type::Kind::kMap:
      return Json::array({"map", TypeToJson(*type.element())});
    case Type::Kind::kObject: {
      Json attributes = Json::object();
      for (const auto& [name, attribute_type] : type.attributes()) {
        attributes[name] = TypeToJson(*attribute_type);
      }
      return Json::array({"object", attributes});
    }
  }
  return nullptr;
}

}  // namespace

std::string Type::ToJson() const { return TypeToJson(*this).dump(); }

// --- Value -----------------------------------------------------------------------------------------------------------

Value Value::Unknown() {
  Value value;
  value.kind_ = Kind::kUnknown;
  return value;
}
Value Value::String(std::string text) {
  Value value;
  value.kind_ = Kind::kString;
  value.string_ = std::move(text);
  return value;
}
Value Value::Bool(bool flag) {
  Value value;
  value.kind_ = Kind::kBool;
  value.bool_ = flag;
  return value;
}
Value Value::Number(int64_t number) {
  Value value;
  value.kind_ = Kind::kNumber;
  value.integer_ = true;
  value.integer_value_ = number;
  return value;
}
Value Value::Number(double number) {
  if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) < kMaxExactInteger) {
    return Number(static_cast<int64_t>(number));
  }
  Value value;
  value.kind_ = Kind::kNumber;
  value.double_ = number;
  return value;
}
Value Value::List(std::vector<Value> elements) {
  Value value;
  value.kind_ = Kind::kList;
  value.elements_ = std::move(elements);
  return value;
}
Value Value::Set(std::vector<Value> elements) {
  std::stable_sort(elements.begin(), elements.end(),
                   [](const Value& a, const Value& b) { return Compare(a, b) < 0; });
  // A set holds each known element once. Unknown elements are all different from each other, so they stay.
  elements.erase(std::unique(elements.begin(), elements.end(),
                             [](const Value& a, const Value& b) {
                               return a.IsKnown() && b.IsKnown() && Compare(a, b) == 0;
                             }),
                 elements.end());
  Value value;
  value.kind_ = Kind::kSet;
  value.elements_ = std::move(elements);
  return value;
}
Value Value::Map(std::map<std::string, Value> entries) {
  Value value;
  value.kind_ = Kind::kMap;
  value.entries_ = std::move(entries);
  return value;
}
Value Value::Object(std::map<std::string, Value> attributes) {
  Value value;
  value.kind_ = Kind::kObject;
  value.entries_ = std::move(attributes);
  return value;
}

const Value& Value::Attr(const std::string& name) const {
  auto found = entries_.find(name);
  return found == entries_.end() ? NullValue() : found->second;
}

bool Value::IsWhollyKnown() const {
  if (kind_ == Kind::kUnknown) return false;
  for (const Value& element : elements_) {
    if (!element.IsWhollyKnown()) return false;
  }
  for (const auto& [name, entry] : entries_) {
    if (!entry.IsWhollyKnown()) return false;
  }
  return true;
}

int Value::Compare(const Value& a, const Value& b) {
  if (a.kind_ != b.kind_) return a.kind_ < b.kind_ ? -1 : 1;
  switch (a.kind_) {
    case Kind::kNull:
    case Kind::kUnknown:
      return 0;
    case Kind::kString:
      return a.string_.compare(b.string_) < 0 ? -1 : (a.string_ == b.string_ ? 0 : 1);
    case Kind::kBool:
      return a.bool_ == b.bool_ ? 0 : (a.bool_ ? 1 : -1);
    case Kind::kNumber:
      if (a.integer_ && b.integer_) {
        return a.integer_value_ == b.integer_value_ ? 0 : (a.integer_value_ < b.integer_value_ ? -1 : 1);
      }
      return a.AsDouble() == b.AsDouble() ? 0 : (a.AsDouble() < b.AsDouble() ? -1 : 1);
    case Kind::kList:
    case Kind::kSet: {
      size_t common = std::min(a.elements_.size(), b.elements_.size());
      for (size_t i = 0; i < common; ++i) {
        if (int order = Compare(a.elements_[i], b.elements_[i]); order != 0) return order;
      }
      return a.elements_.size() == b.elements_.size() ? 0 : (a.elements_.size() < b.elements_.size() ? -1 : 1);
    }
    case Kind::kMap:
    case Kind::kObject: {
      auto left = a.entries_.begin();
      auto right = b.entries_.begin();
      for (; left != a.entries_.end() && right != b.entries_.end(); ++left, ++right) {
        if (int order = left->first.compare(right->first); order != 0) return order < 0 ? -1 : 1;
        if (int order = Compare(left->second, right->second); order != 0) return order;
      }
      if (left == a.entries_.end() && right == b.entries_.end()) return 0;
      return left == a.entries_.end() ? -1 : 1;
    }
  }
  return 0;
}

bool Value::operator==(const Value& other) const {
  // Unknown values are equal to each other here, as in go-cty's RawEquals.
  return Compare(*this, other) == 0;
}

std::string Value::DebugString() const {
  switch (kind_) {
    case Kind::kNull:
      return "null";
    case Kind::kUnknown:
      return "unknown";
    case Kind::kString:
      return Json(string_).dump();
    case Kind::kBool:
      return bool_ ? "true" : "false";
    case Kind::kNumber: {
      if (integer_) return std::to_string(integer_value_);
      char buffer[40];
      std::snprintf(buffer, sizeof buffer, "%.17g", double_);
      return buffer;
    }
    case Kind::kList:
    case Kind::kSet: {
      std::string text = kind_ == Kind::kSet ? "set(" : "[";
      for (size_t i = 0; i < elements_.size(); ++i) {
        if (i > 0) text += ", ";
        text += elements_[i].DebugString();
      }
      return text + (kind_ == Kind::kSet ? ")" : "]");
    }
    case Kind::kMap:
    case Kind::kObject: {
      std::string text = kind_ == Kind::kMap ? "map{" : "{";
      bool first = true;
      for (const auto& [name, entry] : entries_) {
        if (!first) text += ", ";
        first = false;
        text += name + ": " + entry.DebugString();
      }
      return text + "}";
    }
  }
  return "?";
}

// --- Value -> msgpack ------------------------------------------------------------------------------------------------

namespace {

absl::Status Error(const std::string& path, const std::string& message) {
  return absl::InvalidArgumentError(path.empty() ? message : absl::StrCat(path, ": ", message));
}

std::string KeyPath(const std::string& path, const std::string& key) {
  return absl::StrCat(path, "[", Json(key).dump(), "]");
}
std::string IndexPath(const std::string& path, size_t index) { return absl::StrCat(path, "[", index, "]"); }

// Builds the tree that nlohmann turns into msgpack. The type decides what is acceptable, exactly as in go-cty's
// Marshal, which also refuses a value that does not conform.
absl::Status ToTree(const Value& value, const Type& type, const std::string& path, Json* out) {
  if (value.IsUnknown()) {
    *out = Json::binary(std::vector<std::uint8_t>{0x00}, 0);  // msgpack fixext1, type 0: d4 00 00
    return absl::OkStatus();
  }
  if (value.IsNull()) {
    *out = nullptr;
    return absl::OkStatus();
  }
  switch (type.kind()) {
    case Type::Kind::kString:
      if (value.kind() != Value::Kind::kString) return Error(path, "a string is required");
      *out = value.AsString();
      return absl::OkStatus();
    case Type::Kind::kNumber:
      if (value.kind() != Value::Kind::kNumber) return Error(path, "a number is required");
      if (value.IsInteger()) {
        *out = value.AsInt64();
      } else {
        *out = value.AsDouble();
      }
      return absl::OkStatus();
    case Type::Kind::kBool:
      if (value.kind() != Value::Kind::kBool) return Error(path, "a bool is required");
      *out = value.AsBool();
      return absl::OkStatus();
    case Type::Kind::kList:
    case Type::Kind::kSet: {
      const bool is_set = type.kind() == Type::Kind::kSet;
      if (value.kind() != (is_set ? Value::Kind::kSet : Value::Kind::kList)) {
        return Error(path, is_set ? "a set is required" : "a list is required");
      }
      *out = Json::array();
      for (size_t i = 0; i < value.Elements().size(); ++i) {
        Json element;
        if (absl::Status status = ToTree(value.Elements()[i], *type.element(), IndexPath(path, i), &element);
            !status.ok()) {
          return status;
        }
        out->push_back(std::move(element));
      }
      return absl::OkStatus();
    }
    case Type::Kind::kMap: {
      if (value.kind() != Value::Kind::kMap) return Error(path, "a map is required");
      *out = Json::object();
      for (const auto& [key, entry] : value.Entries()) {
        Json element;
        if (absl::Status status = ToTree(entry, *type.element(), KeyPath(path, key), &element); !status.ok()) {
          return status;
        }
        (*out)[key] = std::move(element);
      }
      return absl::OkStatus();
    }
    case Type::Kind::kObject: {
      if (value.kind() != Value::Kind::kObject) return Error(path, "an object is required");
      for (const auto& [name, entry] : value.Entries()) {
        if (!type.attributes().contains(name)) return Error(KeyPath(path, name), "unsupported attribute");
      }
      *out = Json::object();
      for (const auto& [name, attribute_type] : type.attributes()) {
        auto found = value.Entries().find(name);
        if (found == value.Entries().end()) return Error(KeyPath(path, name), "attribute is missing");
        Json element;
        if (absl::Status status = ToTree(found->second, *attribute_type, KeyPath(path, name), &element);
            !status.ok()) {
          return status;
        }
        (*out)[name] = std::move(element);
      }
      return absl::OkStatus();
    }
  }
  return Error(path, "unsupported type");
}

// --- msgpack / JSON -> Value -----------------------------------------------------------------------------------------

// `from_msgpack` is true for the msgpack encoding: there an extension means "unknown" and a number may arrive as a
// string. JSON state has neither.
absl::StatusOr<Value> FromTree(const Json& tree, const Type& type, const std::string& path, bool from_msgpack,
                               bool missing_attributes_are_null) {
  if (tree.is_binary()) {
    // go-cty: a msgpack extension with at most 1 byte of content is a plain unknown; a longer one must be type
    // 0x0c ("unknown with refinements", up to 1 KiB). Anything else is an error. (A plain msgpack `bin` has no
    // subtype and is not an extension.)
    const auto& bytes = tree.get_binary();
    if (!from_msgpack || !bytes.has_subtype()) return Error(path, "unexpected binary value");
    if (bytes.size() <= 1) return Value::Unknown();
    if (bytes.subtype() != 0x0c) {
      return Error(path, absl::StrCat("unsupported extension type 0x", absl::Hex(bytes.subtype(), absl::kZeroPad2),
                                      " with len ", bytes.size()));
    }
    if (bytes.size() > 1024) return Error(path, "oversize unknown value refinement");
    return Value::Unknown();  // the refinements are only hints; they are ignored
  }
  if (tree.is_null()) return Value::Null();

  switch (type.kind()) {
    case Type::Kind::kString:
      if (!tree.is_string()) return Error(path, "string is required");
      return Value::String(tree.get<std::string>());
    case Type::Kind::kBool:
      if (!tree.is_boolean()) return Error(path, "bool is required");
      return Value::Bool(tree.get<bool>());
    case Type::Kind::kNumber: {
      if (tree.is_number_unsigned()) {
        uint64_t number = tree.get<uint64_t>();
        if (number <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
          return Value::Number(static_cast<int64_t>(number));
        }
        return Value::Number(static_cast<double>(number));
      }
      if (tree.is_number_integer()) return Value::Number(tree.get<int64_t>());
      if (tree.is_number_float()) {
        double number = tree.get<double>();
        if (!std::isfinite(number)) return Error(path, "number is required");
        return Value::Number(number);
      }
      if (from_msgpack && tree.is_string()) {
        // go-cty writes a number as a string when it is too large or too precise for an int64 or a float64.
        const std::string& text = tree.get_ref<const std::string&>();
        char* end = nullptr;
        double number = std::strtod(text.c_str(), &end);
        if (!text.empty() && end == text.c_str() + text.size() && std::isfinite(number)) return Value::Number(number);
      }
      return Error(path, "number is required");
    }
    case Type::Kind::kList:
    case Type::Kind::kSet: {
      if (!tree.is_array()) {
        return Error(path, type.kind() == Type::Kind::kSet ? "a set is required" : "a list is required");
      }
      std::vector<Value> elements;
      for (size_t i = 0; i < tree.size(); ++i) {
        absl::StatusOr<Value> element = FromTree(tree[i], *type.element(), IndexPath(path, i), from_msgpack,
                                                 missing_attributes_are_null);
        if (!element.ok()) return element.status();
        elements.push_back(*std::move(element));
      }
      return type.kind() == Type::Kind::kSet ? Value::Set(std::move(elements)) : Value::List(std::move(elements));
    }
    case Type::Kind::kMap: {
      if (!tree.is_object()) return Error(path, "a map is required");
      std::map<std::string, Value> entries;
      for (const auto& [key, entry] : tree.items()) {
        absl::StatusOr<Value> element = FromTree(entry, *type.element(), KeyPath(path, key), from_msgpack,
                                                 missing_attributes_are_null);
        if (!element.ok()) return element.status();
        entries[key] = *std::move(element);
      }
      return Value::Map(std::move(entries));
    }
    case Type::Kind::kObject: {
      if (!tree.is_object()) return Error(path, "an object is required");
      if (!missing_attributes_are_null && tree.size() != type.attributes().size()) {
        return Error(path, absl::StrCat("an object with ", type.attributes().size(), " attributes is required (",
                                        tree.size(), " given)"));
      }
      std::map<std::string, Value> attributes;
      for (const auto& [key, entry] : tree.items()) {
        auto attribute_type = type.attributes().find(key);
        if (attribute_type == type.attributes().end()) return Error(KeyPath(path, key), "unsupported attribute");
        absl::StatusOr<Value> element = FromTree(entry, *attribute_type->second, KeyPath(path, key), from_msgpack,
                                                 missing_attributes_are_null);
        if (!element.ok()) return element.status();
        attributes[key] = *std::move(element);
      }
      for (const auto& [name, attribute_type] : type.attributes()) {
        if (!attributes.contains(name)) attributes[name] = Value::Null();  // only reachable for JSON state
      }
      return Value::Object(std::move(attributes));
    }
  }
  return Error(path, "unsupported type");
}

}  // namespace

absl::StatusOr<std::string> EncodeMsgpack(const Value& value, const Type& type) {
  Json tree;
  if (absl::Status status = ToTree(value, type, "", &tree); !status.ok()) return status;
  std::vector<std::uint8_t> bytes = Json::to_msgpack(tree);
  return std::string(bytes.begin(), bytes.end());
}

absl::StatusOr<Value> DecodeMsgpack(std::string_view bytes, const Type& type) {
  // strict: the whole input must be exactly one value
  Json tree = Json::from_msgpack(bytes, /*strict=*/true, /*allow_exceptions=*/false);
  if (tree.is_discarded()) return absl::InvalidArgumentError("not valid msgpack");
  return FromTree(tree, type, "", /*from_msgpack=*/true, /*missing_attributes_are_null=*/false);
}

absl::StatusOr<Value> DecodeJson(std::string_view json, const Type& type) {
  Json tree = Json::parse(json, nullptr, /*allow_exceptions=*/false);
  if (tree.is_discarded()) return absl::InvalidArgumentError("not valid JSON");
  // An attribute missing from stored state is read as null (state written before the attribute existed).
  return FromTree(tree, type, "", /*from_msgpack=*/false, /*missing_attributes_are_null=*/true);
}

}  // namespace cty
