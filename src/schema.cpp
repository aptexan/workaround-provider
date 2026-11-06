#include "schema.h"

namespace schema {

const Attribute* Block::Find(const std::string& name) const {
  for (const Attribute& attribute : attributes) {
    if (attribute.name == name) return &attribute;
  }
  return nullptr;
}

cty::TypePtr Block::ImpliedType() const {
  std::map<std::string, cty::TypePtr> types;
  for (const Attribute& attribute : attributes) types[attribute.name] = attribute.type;
  return cty::Type::Object(std::move(types));
}

}  // namespace schema
