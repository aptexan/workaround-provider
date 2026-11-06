#pragma once

// The provider's behaviour, as plain functions over cty values: what the provider block and the one resource,
// `workaround_example`, look like, which configurations are valid, and how a change is planned and applied.
// Nothing here knows about gRPC or protobuf; tofu-plugin.cpp translates.
//
// `workaround_example` is a stand-in that needs no outside system, so every part of the protocol can be exercised:
//   id          computed   assigned when the resource is created (unknown while planning), then fixed
//   name        required   1-64 of letters, digits, _ - .  Changing it replaces the resource
//   description optional
//   enabled     optional + computed, default true (the provider fills it in when the configuration leaves it out)
//   size        optional   a whole number from 1 to 100
//   ratio       optional   a number from 0 to 1, fractions allowed
//   tags        optional   map of strings
//   items       optional   list of strings
//   labels      optional   set of strings
//   secret      optional, sensitive
//   summary     computed   derived from name, size and enabled; known at plan time unless one of them is unknown

#include <cstdint>
#include <string>
#include <vector>

#include "cty.h"
#include "schema.h"

namespace workaround {

inline constexpr char kExampleType[] = "workaround_example";

// One step of the path to an attribute (or to an element of one), to point a diagnostic or a replacement at it.
struct PathStep {
  enum class Kind { kAttribute, kKey, kIndex };
  Kind kind = Kind::kAttribute;
  std::string name;  // attribute name, or the key of a map element
  int64_t index = 0;  // position in a list
};
using AttributePath = std::vector<PathStep>;
AttributePath AttributeAt(const std::string& name);
AttributePath KeyAt(AttributePath parent, const std::string& key);

struct Diagnostic {
  enum class Severity { kError, kWarning };
  Severity severity = Severity::kError;
  std::string summary;
  std::string detail;
  AttributePath attribute;  // empty: about the whole block
};

struct ProviderSettings {
  std::string id_prefix = "example";
};

const schema::Block& ProviderSchema();
const schema::Block& ExampleSchema();

// Provider block
std::vector<Diagnostic> ValidateProviderConfig(const cty::Value& config);
ProviderSettings SettingsFrom(const cty::Value& config);

// Resource
std::vector<Diagnostic> ValidateExample(const cty::Value& config);

struct PlanResult {
  cty::Value planned;
  std::vector<AttributePath> requires_replace;
  std::vector<Diagnostic> diagnostics;
};
// `proposed` is what Core suggests: the configuration with the prior value kept for computed attributes. `prior` is
// null when the resource is being created.
PlanResult PlanExample(const cty::Value& prior, const cty::Value& proposed);

// `planned` has unknowns only for computed attributes. A null `planned` means destroy. `id_suffix` becomes part of the
// id of a new resource.
cty::Value ApplyExample(const cty::Value& prior, const cty::Value& planned, const ProviderSettings& settings,
                        const std::string& id_suffix, std::vector<Diagnostic>* diagnostics);

}  // namespace workaround
