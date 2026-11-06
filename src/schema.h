#pragma once

// What a provider tells Core about a configuration block: its attributes, their types and whether each is
// required, optional, computed or sensitive. The block's "implied type" is the object type that every
// DynamicValue for it (config, plan, state) is encoded with.

#include <cstdint>
#include <string>
#include <vector>

#include "cty.h"

namespace schema {

struct Attribute {
  std::string name;
  cty::TypePtr type;
  bool required = false;   // the configuration must set it
  bool optional = false;   // the configuration may set it
  bool computed = false;   // the provider may set it; with optional: the provider supplies a value if the config has none
  bool sensitive = false;  // Core hides the value in its output
  std::string description;
};

struct Block {
  int64_t version = 0;  // bumped when the stored form of a resource changes; see UpgradeResourceState
  std::string description;
  std::vector<Attribute> attributes;

  const Attribute* Find(const std::string& name) const;
  cty::TypePtr ImpliedType() const;
};

}  // namespace schema
