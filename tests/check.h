#pragma once

// The smallest possible test harness, in the same one-line style as tools/lib.sh:
//   Expect("what we check", got, wanted)  prints PASS or FAIL and counts.
// main() ends with `return Summary();`, which is 0 only if everything passed.

#include <cstdio>
#include <iostream>
#include <string>
#include <type_traits>

#include "cty.h"

inline int g_passed = 0;
inline int g_failed = 0;

inline void Expect(const std::string& what, const std::string& got, const std::string& wanted) {
  if (got == wanted) {
    ++g_passed;
    std::cout << "PASS  " << what << "\n";
  } else {
    ++g_failed;
    std::cout << "FAIL  " << what << "\n        wanted: " << wanted << "\n        got:    " << got << "\n";
  }
}
// Only a real bool selects this one: a C string would also convert to bool, so the text is built explicitly.
inline void Expect(const std::string& what, bool got, bool wanted) {
  Expect(what, std::string(got ? "true" : "false"), std::string(wanted ? "true" : "false"));
}
// Anything else that is not text (a count, say) is compared as a number and shown as text on failure.
template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
void Expect(const std::string& what, T got, T wanted) {
  Expect(what, std::to_string(got), std::to_string(wanted));
}
inline void Section(const std::string& title) { std::cout << "== " << title << "\n"; }

// A "negative" check feeds the code something it must refuse and expects the error. With --skip-negative they are left
// out (tools/check-unit.sh passes the option on). Every other check is a "positive" one.
inline bool g_skip_negative = false;
inline void ExpectNegative(const std::string& what, const std::string& got, const std::string& wanted) {
  if (!g_skip_negative) Expect(what, got, wanted);
}
inline void SectionNegative(const std::string& title) { if (!g_skip_negative) Section(title); }
inline int Summary() {
  std::cout << "\npassed " << g_passed << ", failed " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}

// "d4 00 00": bytes as hex, to compare with what the msgpack spec and go-cty say.
inline std::string Hex(const std::string& bytes) {
  std::string text;
  char buffer[4];
  for (unsigned char c : bytes) {
    std::snprintf(buffer, sizeof buffer, "%02x", c);
    if (!text.empty()) text += ' ';
    text += buffer;
  }
  return text;
}
// The other direction, for writing test input: "d4 00 00" -> three bytes.
inline std::string FromHex(const std::string& text) {
  std::string bytes;
  for (size_t i = 0; i + 1 < text.size();) {
    if (text[i] == ' ') { ++i; continue; }
    bytes.push_back(static_cast<char>(std::stoi(text.substr(i, 2), nullptr, 16)));
    i += 2;
  }
  return bytes;
}

// Encode to hex, or "ERROR: <message>".
inline std::string EncodeToHex(const cty::Value& value, const cty::TypePtr& type) {
  auto bytes = cty::EncodeMsgpack(value, *type);
  return bytes.ok() ? Hex(*bytes) : "ERROR: " + std::string(bytes.status().message());
}
// Decode from hex to the value's debug text, or "ERROR: <message>".
inline std::string DecodeHex(const std::string& hex, const cty::TypePtr& type) {
  auto value = cty::DecodeMsgpack(FromHex(hex), *type);
  return value.ok() ? value->DebugString() : "ERROR: " + std::string(value.status().message());
}
inline std::string DecodeJsonText(const std::string& json, const cty::TypePtr& type) {
  auto value = cty::DecodeJson(json, *type);
  return value.ok() ? value->DebugString() : "ERROR: " + std::string(value.status().message());
}
