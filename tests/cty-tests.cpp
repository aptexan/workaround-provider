// Tests of the value model and its msgpack/JSON encodings, against byte sequences worked out from the msgpack
// specification and from the go-cty (v1.19.0) source that OpenTofu uses.

#include "check.h"
#include "cty.h"

using cty::Type;
using cty::Value;

namespace {

const cty::TypePtr kString = Type::String();
const cty::TypePtr kNumber = Type::Number();
const cty::TypePtr kBool = Type::Bool();

void TypeJson() {
  Section("type JSON, as a schema carries it");
  Expect("string", kString->ToJson(), R"("string")");
  Expect("number", kNumber->ToJson(), R"("number")");
  Expect("bool", kBool->ToJson(), R"("bool")");
  Expect("list of string", Type::List(kString)->ToJson(), R"(["list","string"])");
  Expect("set of number", Type::Set(kNumber)->ToJson(), R"(["set","number"])");
  Expect("map of bool", Type::Map(kBool)->ToJson(), R"(["map","bool"])");
  Expect("object", Type::Object({{"b", Type::Map(kBool)}, {"a", kNumber}})->ToJson(),
         R"(["object",{"a":"number","b":["map","bool"]}])");
}

void Values() {
  Section("values");
  Expect("a whole double equals the int", Value::Number(3.0) == Value::Number(int64_t{3}), true);
  Expect("a whole double is stored as an integer", Value::Number(3.0).IsInteger(), true);
  Expect("1.5 is not an integer", Value::Number(1.5).IsInteger(), false);
  Expect("unknown equals unknown", Value::Unknown() == Value::Unknown(), true);
  Expect("null is not unknown", Value::Null() == Value::Unknown(), false);
  Expect("a set sorts and drops duplicates",
         Value::Set({Value::String("b"), Value::String("a"), Value::String("b")}).DebugString(),
         R"(set("a", "b"))");
  Expect("a list keeps order and duplicates",
         Value::List({Value::String("b"), Value::String("a"), Value::String("b")}).DebugString(),
         R"(["b", "a", "b"])");
  Expect("an object with an unknown inside is not wholly known",
         Value::Object({{"a", Value::List({Value::Unknown()})}}).IsWhollyKnown(), false);
  Expect("a missing attribute reads as null", Value::Object({}).Attr("nope").IsNull(), true);
}

void EncodeScalars() {
  Section("msgpack encode: scalars");
  Expect("null", EncodeToHex(Value::Null(), kString), "c0");
  Expect("unknown (go-cty: fixext1, type 0)", EncodeToHex(Value::Unknown(), kString), "d4 00 00");
  Expect("unknown number", EncodeToHex(Value::Unknown(), kNumber), "d4 00 00");
  Expect("string a", EncodeToHex(Value::String("a"), kString), "a1 61");
  Expect("empty string", EncodeToHex(Value::String(""), kString), "a0");
  Expect("a 31-byte string is still a fixstr", EncodeToHex(Value::String(std::string(31, 'x')), kString).substr(0, 5), "bf 78");
  Expect("a 32-byte string needs str8", EncodeToHex(Value::String(std::string(32, 'x')), kString).substr(0, 8), "d9 20 78");
  Expect("true", EncodeToHex(Value::Bool(true), kBool), "c3");
  Expect("false", EncodeToHex(Value::Bool(false), kBool), "c2");
}

void EncodeNumbers() {
  Section("msgpack encode: numbers use the smallest integer form");
  auto number = [](int64_t n) { return EncodeToHex(Value::Number(n), kNumber); };
  Expect("0", number(0), "00");
  Expect("127", number(127), "7f");
  Expect("128", number(128), "cc 80");
  Expect("255", number(255), "cc ff");
  Expect("256", number(256), "cd 01 00");
  Expect("65535", number(65535), "cd ff ff");
  Expect("65536", number(65536), "ce 00 01 00 00");
  Expect("-1", number(-1), "ff");
  Expect("-32", number(-32), "e0");
  Expect("-33", number(-33), "d0 df");
  Expect("-128", number(-128), "d0 80");
  Expect("-129", number(-129), "d1 ff 7f");
  Expect("int64 max", number(INT64_MAX), "cf 7f ff ff ff ff ff ff ff");
  Expect("int64 min", number(INT64_MIN), "d3 80 00 00 00 00 00 00 00");
  Expect("3.0 is written as the integer 3", EncodeToHex(Value::Number(3.0), kNumber), "03");
  // go-cty always writes a float64 (cb ..). nlohmann writes a float32 (ca ..) when that holds the value exactly. Both are
  // valid msgpack and Core's decoder reads both through the same branch, so this is a harmless difference in bytes.
  Expect("1.5 fits a float32, written as one", EncodeToHex(Value::Number(1.5), kNumber), "ca 3f c0 00 00");
  Expect("0.1 does not fit a float32, written as a float64", EncodeToHex(Value::Number(0.1), kNumber),
         "cb 3f b9 99 99 99 99 99 9a");
}

void EncodeCollections() {
  Section("msgpack encode: collections and objects");
  Expect("list of two strings", EncodeToHex(Value::List({Value::String("a"), Value::String("b")}), Type::List(kString)),
         "92 a1 61 a1 62");
  Expect("empty list", EncodeToHex(Value::List({}), Type::List(kString)), "90");
  Expect("set is an array, sorted and unique",
         EncodeToHex(Value::Set({Value::String("b"), Value::String("a"), Value::String("a")}), Type::Set(kString)),
         "92 a1 61 a1 62");
  Expect("map", EncodeToHex(Value::Map({{"k", Value::String("v")}}), Type::Map(kString)), "81 a1 6b a1 76");
  auto object_type = Type::Object({{"b", kNumber}, {"a", kString}});
  Expect("object: every attribute, sorted by name",
         EncodeToHex(Value::Object({{"b", Value::Number(int64_t{1})}, {"a", Value::String("x")}}), object_type),
         "82 a1 61 a1 78 a1 62 01");
  Expect("object with an unknown and a null",
         EncodeToHex(Value::Object({{"a", Value::Unknown()}, {"b", Value::Null()}}), object_type),
         "82 a1 61 d4 00 00 a1 62 c0");
}

void EncodeErrors() {
  SectionNegative("msgpack encode: a value that does not fit its type is refused");
  ExpectNegative("number for a string", EncodeToHex(Value::Number(int64_t{1}), kString), "ERROR: a string is required");
  ExpectNegative("string for a bool", EncodeToHex(Value::String("x"), kBool), "ERROR: a bool is required");
  ExpectNegative("the path to a bad list element",
         EncodeToHex(Value::List({Value::String("a"), Value::Bool(true)}), Type::List(kString)),
         "ERROR: [1]: a string is required");
  ExpectNegative("the path to a bad map entry",
         EncodeToHex(Value::Map({{"env", Value::Bool(true)}}), Type::Map(kString)),
         R"(ERROR: ["env"]: a string is required)");
  auto object_type = Type::Object({{"a", kString}, {"b", kString}});
  ExpectNegative("object missing an attribute", EncodeToHex(Value::Object({{"a", Value::String("x")}}), object_type),
         R"(ERROR: ["b"]: attribute is missing)");
  ExpectNegative("object with an extra attribute",
         EncodeToHex(Value::Object({{"a", Value::Null()}, {"b", Value::Null()}, {"z", Value::Null()}}), object_type),
         R"(ERROR: ["z"]: unsupported attribute)");
}

void DecodeUnknowns() {
  Section("msgpack decode: unknown values, as go-cty reads them");
  Expect("d4 00 00", DecodeHex("d4 00 00", kString), "unknown");
  Expect("any extension of at most 1 byte is unknown", DecodeHex("d4 05 00", kNumber), "unknown");
  Expect("an empty extension is unknown", DecodeHex("c7 00 07", kString), "unknown");
  Expect("type 12 with refinements is unknown (refinements ignored)", DecodeHex("c7 03 0c 81 01 c2", kString), "unknown");
  ExpectNegative("another extension type with content is refused", DecodeHex("c7 03 05 81 01 c2", kString),
         "ERROR: unsupported extension type 0x05 with len 3");
  Expect("an unknown inside a list", DecodeHex("92 a1 61 d4 00 00", Type::List(kString)), R"(["a", unknown])");
}

void DecodeNumbers() {
  Section("msgpack decode: a number may arrive in several forms");
  Expect("fixint", DecodeHex("03", kNumber), "3");
  Expect("uint8", DecodeHex("cc 03", kNumber), "3");
  Expect("int8", DecodeHex("d0 fd", kNumber), "-3");
  Expect("float64 3.0 is the integer 3", DecodeHex("cb 40 08 00 00 00 00 00 00", kNumber), "3");
  Expect("float64 1.5", DecodeHex("cb 3f f8 00 00 00 00 00 00", kNumber), "1.5");
  Expect("float32 1.5", DecodeHex("ca 3f c0 00 00", kNumber), "1.5");
  Expect("a string holding a whole number", DecodeHex("a1 33", kNumber), "3");
  Expect("a string holding a fraction", DecodeHex("a3 31 2e 35", kNumber), "1.5");
  ExpectNegative("a string that is not a number", DecodeHex("a1 78", kNumber), "ERROR: number is required");
  Expect("uint64 max does not fit an int64, becomes a float",
         cty::DecodeMsgpack(FromHex("cf ff ff ff ff ff ff ff ff"), *kNumber)->IsInteger(), false);
}

void DecodeOthers() {
  Section("msgpack decode: null, shapes and mistakes");
  Expect("nil is null for a string", DecodeHex("c0", kString), "null");
  Expect("nil is null for an object", DecodeHex("c0", Type::Object({{"a", kString}})), "null");
  Expect("a bool", DecodeHex("c3", kBool), "true");
  Expect("a string", DecodeHex("a2 68 69", kString), R"("hi")");
  Expect("a set arrives with duplicates removed", DecodeHex("93 a1 62 a1 61 a1 62", Type::Set(kString)), R"(set("a", "b"))");
  Expect("a map", DecodeHex("82 a1 62 01 a1 61 02", Type::Map(kNumber)), "map{a: 2, b: 1}");
  ExpectNegative("a string where a number is expected", DecodeHex("a1 61", kBool), "ERROR: bool is required");
  ExpectNegative("a number where a string is expected", DecodeHex("01", kString), "ERROR: string is required");
  auto object_type = Type::Object({{"a", kString}, {"b", kString}});
  Expect("an object", DecodeHex("82 a1 61 a1 78 a1 62 c0", object_type), R"({a: "x", b: null})");
  ExpectNegative("an object with too few attributes", DecodeHex("81 a1 61 a1 78", object_type),
         "ERROR: an object with 2 attributes is required (1 given)");
  ExpectNegative("an empty object where attributes are required", DecodeHex("80", object_type),
         "ERROR: an object with 2 attributes is required (0 given)");
  ExpectNegative("an unknown attribute name", DecodeHex("82 a1 61 c0 a1 7a c0", object_type), R"(ERROR: ["z"]: unsupported attribute)");
  Expect("an empty object for a type with no attributes", DecodeHex("80", Type::Object({})), "{}");
  ExpectNegative("the path to a bad nested value", DecodeHex("82 a1 61 01 a1 62 c0", object_type), R"(ERROR: ["a"]: string is required)");
  ExpectNegative("trailing bytes", DecodeHex("c0 c0", kString), "ERROR: not valid msgpack");
  ExpectNegative("truncated input", DecodeHex("92 a1 61", Type::List(kString)), "ERROR: not valid msgpack");
  ExpectNegative("empty input", DecodeHex("", kString), "ERROR: not valid msgpack");
}

void RoundTrip() {
  Section("msgpack round trip");
  auto type = Type::Object({{"name", kString},
                            {"size", kNumber},
                            {"on", kBool},
                            {"tags", Type::Map(kString)},
                            {"items", Type::List(kString)},
                            {"labels", Type::Set(kString)},
                            {"nested", Type::Object({{"x", kNumber}})}});
  Value value = Value::Object({
      {"name", Value::String("web")},
      {"size", Value::Number(1.25)},
      {"on", Value::Unknown()},
      {"tags", Value::Map({{"a", Value::String("1")}, {"b", Value::Null()}})},
      {"items", Value::List({Value::String("z"), Value::Unknown(), Value::String("z")})},
      {"labels", Value::Set({Value::String("q"), Value::String("p")})},
      {"nested", Value::Object({{"x", Value::Number(int64_t{-7})}})},
  });
  auto bytes = cty::EncodeMsgpack(value, *type);
  auto back = bytes.ok() ? cty::DecodeMsgpack(*bytes, *type) : absl::StatusOr<Value>(bytes.status());
  Expect("a mixed value comes back equal", back.ok() && *back == value, true);
  Expect("encoding twice gives the same bytes", cty::EncodeMsgpack(value, *type).value() == *bytes, true);
}

void Json() {
  Section("JSON state (UpgradeResourceState)");
  auto object_type = Type::Object({{"a", kString}, {"n", kNumber}, {"l", Type::List(kString)}, {"s", Type::Set(kString)},
                                   {"m", Type::Map(kBool)}, {"b", kBool}});
  Expect("every kind", DecodeJsonText(R"({"a":"x","n":1.5,"l":["p","q"],"s":["y","x","y"],"m":{"k":true},"b":false})", object_type),
         R"({a: "x", b: false, l: ["p", "q"], m: map{k: true}, n: 1.5, s: set("x", "y")})");
  Expect("a missing attribute is null (state older than the attribute)", DecodeJsonText(R"({"a":"x"})", object_type),
         R"({a: "x", b: null, l: null, m: null, n: null, s: null})");
  ExpectNegative("an attribute the schema does not have", DecodeJsonText(R"({"zzz":1})", object_type), R"(ERROR: ["zzz"]: unsupported attribute)");
  ExpectNegative("a number is not accepted as a string", DecodeJsonText(R"({"a":1})", object_type), R"(ERROR: ["a"]: string is required)");
  ExpectNegative("a number held in a string is NOT a number in JSON state", DecodeJsonText(R"({"n":"3"})", object_type), R"(ERROR: ["n"]: number is required)");
  Expect("null", DecodeJsonText("null", object_type), "null");
  Expect("an integer too large for a double stays exact (2^53 + 1)", DecodeJsonText(R"({"n":9007199254740993})", object_type),
         "{a: null, b: null, l: null, m: null, n: 9007199254740993, s: null}");
  ExpectNegative("not JSON", DecodeJsonText("{oops", object_type), "ERROR: not valid JSON");
}

}  // namespace

void RunCtyTests() {
  TypeJson();
  Values();
  EncodeScalars();
  EncodeNumbers();
  EncodeCollections();
  EncodeErrors();
  DecodeUnknowns();
  DecodeNumbers();
  DecodeOthers();
  RoundTrip();
  Json();
}
