#include "workaround-example.h"

#include <cmath>
#include <cstdio>

namespace workaround {

using cty::Type;
using cty::Value;

AttributePath AttributeAt(const std::string& name) {
  return {PathStep{PathStep::Kind::kAttribute, name, 0}};
}

AttributePath KeyAt(AttributePath parent, const std::string& key) {
  parent.push_back(PathStep{PathStep::Kind::kKey, key, 0});
  return parent;
}

namespace {

schema::Attribute Attribute(std::string name, cty::TypePtr type, std::string description) {
  schema::Attribute attribute;
  attribute.name = std::move(name);
  attribute.type = std::move(type);
  attribute.description = std::move(description);
  return attribute;
}
schema::Attribute Required(schema::Attribute attribute) {
  attribute.required = true;
  return attribute;
}
schema::Attribute Optional(schema::Attribute attribute) {
  attribute.optional = true;
  return attribute;
}
schema::Attribute Computed(schema::Attribute attribute) {
  attribute.computed = true;
  return attribute;
}
schema::Attribute Sensitive(schema::Attribute attribute) {
  attribute.sensitive = true;
  return attribute;
}

Diagnostic Error(std::string summary, std::string detail, AttributePath attribute) {
  return Diagnostic{Diagnostic::Severity::kError, std::move(summary), std::move(detail), std::move(attribute)};
}
Diagnostic Warning(std::string summary, std::string detail, AttributePath attribute) {
  return Diagnostic{Diagnostic::Severity::kWarning, std::move(summary), std::move(detail), std::move(attribute)};
}

bool IsKnownAndSet(const Value& value) { return value.IsKnown() && !value.IsNull(); }

// "5" for a whole number, otherwise the shortest text that reads back the same.
std::string FormatNumber(const Value& number) {
  if (number.IsInteger()) return std::to_string(number.AsInt64());
  char buffer[40];
  std::snprintf(buffer, sizeof buffer, "%.15g", number.AsDouble());
  return buffer;
}

// The text of `summary`, or unknown while name, size or enabled is not known yet.
Value Summary(const std::map<std::string, Value>& attributes) {
  const Value& name = attributes.at("name");
  const Value& size = attributes.at("size");
  const Value& enabled = attributes.at("enabled");
  if (name.IsUnknown() || size.IsUnknown() || enabled.IsUnknown()) return Value::Unknown();
  return Value::String((name.IsNull() ? "" : name.AsString()) + ": size=" +
                       (size.IsNull() ? "none" : FormatNumber(size)) + ", enabled=" +
                       (enabled.IsNull() || enabled.AsBool() ? "true" : "false"));
}

}  // namespace

const schema::Block& ProviderSchema() {
  static const schema::Block kSchema = [] {
    schema::Block block;
    block.description = "The workaround provider.";
    block.attributes = {
        Optional(Attribute("id_prefix", Type::String(),
                           "Prefix of the ids that resources get, 1 to 16 characters. Default: example.")),
    };
    return block;
  }();
  return kSchema;
}

const schema::Block& ExampleSchema() {
  static const schema::Block kSchema = [] {
    schema::Block block;
    block.version = 1;
    block.description = "An example resource that keeps its settings in state and needs no outside system.";
    block.attributes = {
        Computed(Attribute("id", Type::String(), "Assigned when the resource is created.")),
        Required(Attribute("name", Type::String(), "1 to 64 of letters, digits, _, - and . Changing it replaces the resource.")),
        Optional(Attribute("description", Type::String(), "Free text. Ignored while enabled is false.")),
        Computed(Optional(Attribute("enabled", Type::Bool(), "Default: true."))),
        Optional(Attribute("size", Type::Number(), "A whole number from 1 to 100.")),
        Optional(Attribute("ratio", Type::Number(), "A number from 0 to 1; fractions are allowed.")),
        Optional(Attribute("tags", Type::Map(Type::String()), "Key-value tags; keys must not be empty.")),
        Optional(Attribute("items", Type::List(Type::String()), "An ordered list of strings.")),
        Optional(Attribute("labels", Type::Set(Type::String()), "An unordered set of strings.")),
        Sensitive(Optional(Attribute("secret", Type::String(), "Hidden in output."))),
        Computed(Attribute("summary", Type::String(), "Derived from name, size and enabled.")),
    };
    return block;
  }();
  return kSchema;
}

// --- provider block --------------------------------------------------------------------------------------------------

std::vector<Diagnostic> ValidateProviderConfig(const Value& config) {
  std::vector<Diagnostic> diagnostics;
  const Value& prefix = config.Attr("id_prefix");
  if (IsKnownAndSet(prefix)) {
    const std::string& text = prefix.AsString();
    bool valid = !text.empty() && text.size() <= 16 && text[0] >= 'a' && text[0] <= 'z';
    for (char c : text) {
      valid = valid && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-');
    }
    if (!valid) {
      diagnostics.push_back(Error(
          "Invalid id_prefix",
          "id_prefix must be 1 to 16 characters: a lowercase letter, then lowercase letters, digits or '-'.",
          AttributeAt("id_prefix")));
    }
  }
  return diagnostics;
}

ProviderSettings SettingsFrom(const Value& config) {
  ProviderSettings settings;
  const Value& prefix = config.Attr("id_prefix");
  if (IsKnownAndSet(prefix)) settings.id_prefix = prefix.AsString();
  return settings;
}

// --- validation ------------------------------------------------------------------------------------------------------

std::vector<Diagnostic> ValidateExample(const Value& config) {
  std::vector<Diagnostic> diagnostics;

  const Value& name = config.Attr("name");
  if (IsKnownAndSet(name)) {
    const std::string& text = name.AsString();
    bool valid = !text.empty() && text.size() <= 64;
    for (char c : text) {
      valid = valid && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.');
    }
    if (!valid) {
      diagnostics.push_back(Error("Invalid name",
                                  "name must be 1 to 64 characters of letters, digits, '_', '-' or '.'.",
                                  AttributeAt("name")));
    }
  }

  const Value& size = config.Attr("size");
  if (IsKnownAndSet(size)) {
    bool valid = size.IsInteger() && size.AsInt64() >= 1 && size.AsInt64() <= 100;
    if (!valid) {
      diagnostics.push_back(Error("Invalid size", "size must be a whole number between 1 and 100.",
                                  AttributeAt("size")));
    }
  }

  const Value& ratio = config.Attr("ratio");
  if (IsKnownAndSet(ratio) && !(ratio.AsDouble() >= 0 && ratio.AsDouble() <= 1)) {
    diagnostics.push_back(Error("Invalid ratio", "ratio must be a number from 0 to 1.", AttributeAt("ratio")));
  }

  const Value& tags = config.Attr("tags");
  if (IsKnownAndSet(tags) && tags.kind() == Value::Kind::kMap) {
    for (const auto& [key, tag] : tags.Entries()) {
      if (key.empty()) {
        diagnostics.push_back(Error("Invalid tags", "tag names must not be empty.", KeyAt(AttributeAt("tags"), key)));
      }
    }
  }

  const Value& enabled = config.Attr("enabled");
  if (IsKnownAndSet(config.Attr("description")) && IsKnownAndSet(enabled) && !enabled.AsBool()) {
    diagnostics.push_back(Warning("description has no effect", "description is ignored while enabled is false.",
                                  AttributeAt("description")));
  }
  return diagnostics;
}

// --- planning and applying -------------------------------------------------------------------------------------------

PlanResult PlanExample(const Value& prior, const Value& proposed) {
  PlanResult result;
  if (proposed.IsNull() || proposed.IsUnknown()) {
    result.planned = proposed;  // destroying (or nothing known yet): nothing to decide
    return result;
  }
  std::map<std::string, Value> planned = proposed.Entries();

  // enabled is optional + computed: when the configuration leaves it out, the provider supplies the default.
  // (Core proposes the prior value for an update and null for a create.)
  if (planned["enabled"].IsNull()) planned["enabled"] = Value::Bool(true);

  // id is assigned when the resource is created, so it is unknown while planning; afterwards it never changes.
  planned["id"] = prior.IsNull() ? Value::Unknown() : prior.Attr("id");

  planned["summary"] = Summary(planned);

  // A new name is a different resource.
  if (!prior.IsNull() && prior.Attr("name") != planned["name"]) {
    result.requires_replace.push_back(AttributeAt("name"));
  }
  result.planned = Value::Object(std::move(planned));
  return result;
}

Value ApplyExample(const Value& prior, const Value& planned, const ProviderSettings& settings,
                   const std::string& id_suffix, std::vector<Diagnostic>* diagnostics) {
  if (planned.IsNull()) return Value::Null();  // destroy: there is no outside system to clean up

  std::map<std::string, Value> attributes = planned.Entries();
  if (!attributes["id"].IsKnown() || attributes["id"].IsNull()) {
    attributes["id"] = prior.IsNull() ? Value::String(settings.id_prefix + "-" + id_suffix) : prior.Attr("id");
  }
  attributes["summary"] = Summary(attributes);

  Value result = Value::Object(std::move(attributes));
  if (!result.IsWhollyKnown()) {
    diagnostics->push_back(Error("Incomplete plan",
                                 "The plan still has unknown values at apply time: " + result.DebugString(),
                                 AttributePath{}));
  }
  return result;
}

}  // namespace workaround
