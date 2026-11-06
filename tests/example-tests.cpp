// Tests of the provider's rules for the provider block and for `workaround_example`: what is valid, and how a change
// is planned and applied. These are the same rules Core enforces on a plan (objchange.AssertPlanValid): nothing but
// null computed attributes may be filled in, and an unknown may stand only for what is not known yet.

#include <map>

#include "check.h"
#include "workaround-example.h"

using cty::Value;
using workaround::AttributeAt;
using workaround::Diagnostic;

namespace {

// An example resource value: the attributes given, every other attribute null (as in a decoded configuration).
Value Example(std::map<std::string, Value> given) {
  std::map<std::string, Value> all;
  for (const auto& attribute : workaround::ExampleSchema().attributes) all[attribute.name] = Value::Null();
  for (auto& [name, value] : given) all[name] = std::move(value);
  return Value::Object(std::move(all));
}
Value Str(const char* text) { return Value::String(text); }
Value Num(int64_t number) { return Value::Number(number); }

// "error: Invalid size [size]" for each diagnostic, joined with "; ", or "none".
std::string Describe(const std::vector<Diagnostic>& diagnostics) {
  if (diagnostics.empty()) return "none";
  std::string text;
  for (const Diagnostic& d : diagnostics) {
    if (!text.empty()) text += "; ";
    text += d.severity == Diagnostic::Severity::kError ? "error: " : "warning: ";
    text += d.summary + " [";
    for (const auto& step : d.attribute) text += step.name + (&step == &d.attribute.back() ? "" : ".");
    text += "]";
  }
  return text;
}
std::string DescribePaths(const std::vector<workaround::AttributePath>& paths) {
  std::string text;
  for (const auto& path : paths) {
    if (!text.empty()) text += ", ";
    for (const auto& step : path) text += step.name;
  }
  return text.empty() ? "none" : text;
}

void Schemas() {
  Section("schemas");
  const auto& schema = workaround::ExampleSchema();
  auto flags = [&](const char* name) {
    const schema::Attribute* a = schema.Find(name);
    if (a == nullptr) return std::string("missing");
    return std::string(a->required ? "required " : "") + (a->optional ? "optional " : "") +
           (a->computed ? "computed " : "") + (a->sensitive ? "sensitive" : "");
  };
  Expect("id", flags("id"), "computed ");
  Expect("name", flags("name"), "required ");
  Expect("enabled has a provider default", flags("enabled"), "optional computed ");
  Expect("secret", flags("secret"), "optional sensitive");
  Expect("summary", flags("summary"), "computed ");
  Expect("no attribute is called count (a reserved name in Terraform)", schema.Find("count") == nullptr, true);
  Expect("the implied type is an object of all attributes", workaround::ExampleSchema().ImpliedType()->attributes().size(), schema.attributes.size());
  Expect("resource schema version", std::to_string(schema.version), "1");
  Expect("provider block", workaround::ProviderSchema().attributes[0].name, "id_prefix");
}

void ProviderBlock() {
  Section("provider block");
  auto check = [](const char* what, Value prefix) {
    return std::string(what) + ": " +
           Describe(workaround::ValidateProviderConfig(Value::Object({{"id_prefix", std::move(prefix)}})));
  };
  Expect("a good prefix", check("wp", Str("wp")), "wp: none");
  Expect("not set", check("null", Value::Null()), "null: none");
  Expect("unknown is not judged", check("unknown", Value::Unknown()), "unknown: none");
  ExpectNegative("uppercase", check("Bad", Str("Bad")), "Bad: error: Invalid id_prefix [id_prefix]");
  ExpectNegative("empty", check("empty", Str("")), "empty: error: Invalid id_prefix [id_prefix]");
  ExpectNegative("starts with a digit", check("1a", Str("1a")), "1a: error: Invalid id_prefix [id_prefix]");
  ExpectNegative("17 characters", check("long", Str("abcdefghijklmnopq")), "long: error: Invalid id_prefix [id_prefix]");
  Expect("16 characters", check("max", Str("abcdefghijklmnop")), "max: none");
  Expect("default prefix", workaround::SettingsFrom(Value::Object({{"id_prefix", Value::Null()}})).id_prefix, "example");
  Expect("configured prefix", workaround::SettingsFrom(Value::Object({{"id_prefix", Str("wp")}})).id_prefix, "wp");
  Expect("unknown prefix falls back to the default", workaround::SettingsFrom(Value::Object({{"id_prefix", Value::Unknown()}})).id_prefix, "example");
}

void Validation() {
  Section("resource validation");
  auto validate = [](Value config) { return Describe(workaround::ValidateExample(config)); };
  Expect("a minimal config", validate(Example({{"name", Str("web")}})), "none");
  ExpectNegative("an empty name", validate(Example({{"name", Str("")}})), "error: Invalid name [name]");
  ExpectNegative("a space in the name", validate(Example({{"name", Str("a b")}})), "error: Invalid name [name]");
  ExpectNegative("a 65-character name", validate(Example({{"name", Str(std::string(65, 'a').c_str())}})), "error: Invalid name [name]");
  Expect("a 64-character name", validate(Example({{"name", Str(std::string(64, 'a').c_str())}})), "none");
  Expect("every allowed character", validate(Example({{"name", Str("Az09_-.")}})), "none");
  Expect("an unknown name is not judged", validate(Example({{"name", Value::Unknown()}})), "none");
  ExpectNegative("size 0", validate(Example({{"name", Str("a")}, {"size", Num(0)}})), "error: Invalid size [size]");
  ExpectNegative("size 101", validate(Example({{"name", Str("a")}, {"size", Num(101)}})), "error: Invalid size [size]");
  Expect("size 100", validate(Example({{"name", Str("a")}, {"size", Num(100)}})), "none");
  Expect("size 1", validate(Example({{"name", Str("a")}, {"size", Num(1)}})), "none");
  ExpectNegative("size 2.5", validate(Example({{"name", Str("a")}, {"size", Value::Number(2.5)}})), "error: Invalid size [size]");
  Expect("ratio 0.5", validate(Example({{"name", Str("a")}, {"ratio", Value::Number(0.5)}})), "none");
  Expect("ratio 0 and 1", validate(Example({{"name", Str("a")}, {"ratio", Value::Number(int64_t{0})}})) + validate(Example({{"name", Str("a")}, {"ratio", Value::Number(int64_t{1})}})), "nonenone");
  ExpectNegative("ratio -0.1", validate(Example({{"name", Str("a")}, {"ratio", Value::Number(-0.1)}})), "error: Invalid ratio [ratio]");
  ExpectNegative("ratio 1.01", validate(Example({{"name", Str("a")}, {"ratio", Value::Number(1.01)}})), "error: Invalid ratio [ratio]");
  Expect("an unknown ratio is not judged", validate(Example({{"name", Str("a")}, {"ratio", Value::Unknown()}})), "none");
  Expect("an unknown size is not judged", validate(Example({{"name", Str("a")}, {"size", Value::Unknown()}})), "none");
  ExpectNegative("an empty tag name",
         validate(Example({{"name", Str("a")}, {"tags", Value::Map({{"", Str("x")}})}})), "error: Invalid tags [tags.]");
  ExpectNegative("two problems are both reported",
         validate(Example({{"name", Str("")}, {"size", Num(0)}})), "error: Invalid name [name]; error: Invalid size [size]");
  Expect("a description with enabled false is only a warning",
         validate(Example({{"name", Str("a")}, {"description", Str("d")}, {"enabled", Value::Bool(false)}})),
         "warning: description has no effect [description]");
  Expect("a description with enabled true is fine",
         validate(Example({{"name", Str("a")}, {"description", Str("d")}, {"enabled", Value::Bool(true)}})), "none");
  Expect("the empty-tag diagnostic points at the map element",
         workaround::ValidateExample(Example({{"name", Str("a")}, {"tags", Value::Map({{"", Str("x")}})}}))[0].attribute.back().kind ==
             workaround::PathStep::Kind::kKey, true);
}

Value Created(const char* id, std::map<std::string, Value> given) {
  given["id"] = Str(id);
  return Example(std::move(given));
}

void Planning() {
  Section("planning");
  // Core proposes the configuration, with null for the computed attributes of a resource that does not exist yet.
  auto create = workaround::PlanExample(Value::Null(), Example({{"name", Str("web")}}));
  Expect("create: id is unknown until apply", create.planned.Attr("id").IsUnknown(), true);
  Expect("create: enabled gets the provider default", create.planned.Attr("enabled").DebugString(), "true");
  Expect("create: summary is already known", create.planned.Attr("summary").DebugString(), R"("web: size=none, enabled=true")");
  Expect("create: nothing to replace", DescribePaths(create.requires_replace), "none");
  Expect("create: no diagnostics", Describe(create.diagnostics), "none");
  Expect("create: configured values pass through",
         workaround::PlanExample(Value::Null(), Example({{"name", Str("web")}, {"size", Num(7)}, {"description", Str("d")}}))
             .planned.Attr("description").DebugString(), R"("d")");

  auto explicit_false = workaround::PlanExample(Value::Null(), Example({{"name", Str("a")}, {"enabled", Value::Bool(false)}}));
  Expect("an explicit enabled=false is kept", explicit_false.planned.Attr("enabled").DebugString(), "false");
  Expect("... and shows in the summary", explicit_false.planned.Attr("summary").DebugString(), R"("a: size=none, enabled=false")");

  auto unknown_name = workaround::PlanExample(Value::Null(), Example({{"name", Value::Unknown()}}));
  Expect("an unknown name makes the summary unknown", unknown_name.planned.Attr("summary").IsUnknown(), true);
  auto unknown_size = workaround::PlanExample(Value::Null(), Example({{"name", Str("a")}, {"size", Value::Unknown()}}));
  Expect("an unknown size makes the summary unknown", unknown_size.planned.Attr("summary").IsUnknown(), true);

  Value prior = Created("wp-1", {{"name", Str("web")}, {"size", Num(3)}, {"enabled", Value::Bool(true)},
                                 {"summary", Str("web: size=3, enabled=true")}});
  // For an update Core proposes the configuration, with the prior value for computed attributes the config leaves null.
  auto unchanged = workaround::PlanExample(prior, Created("wp-1", {{"name", Str("web")}, {"size", Num(3)},
                                                                   {"enabled", Value::Bool(true)},
                                                                   {"summary", Str("web: size=3, enabled=true")}}));
  Expect("no change: the plan equals the prior state", unchanged.planned == prior, true);
  Expect("no change: nothing to replace", DescribePaths(unchanged.requires_replace), "none");

  auto resized = workaround::PlanExample(prior, Created("wp-1", {{"name", Str("web")}, {"size", Num(9)},
                                                                 {"enabled", Value::Bool(true)},
                                                                 {"summary", Str("web: size=3, enabled=true")}}));
  Expect("update: id is kept", resized.planned.Attr("id").DebugString(), R"("wp-1")");
  Expect("update: summary follows the new size", resized.planned.Attr("summary").DebugString(), R"("web: size=9, enabled=true")");
  Expect("update: in place, nothing to replace", DescribePaths(resized.requires_replace), "none");

  auto renamed = workaround::PlanExample(prior, Created("wp-1", {{"name", Str("web2")}, {"size", Num(3)},
                                                                 {"enabled", Value::Bool(true)},
                                                                 {"summary", Str("web: size=3, enabled=true")}}));
  Expect("a new name requires replacement", DescribePaths(renamed.requires_replace), "name");
  auto renamed_unknown = workaround::PlanExample(prior, Created("wp-1", {{"name", Value::Unknown()}, {"size", Num(3)},
                                                                         {"enabled", Value::Bool(true)}}));
  Expect("a name that is not known yet also requires replacement", DescribePaths(renamed_unknown.requires_replace), "name");

  auto kept_disabled = workaround::PlanExample(
      Created("wp-1", {{"name", Str("a")}, {"enabled", Value::Bool(false)}}),
      Created("wp-1", {{"name", Str("a")}, {"enabled", Value::Bool(false)}}));
  Expect("enabled left out of the config keeps the prior value (Core proposes it)", kept_disabled.planned.Attr("enabled").DebugString(), "false");

  Expect("destroy plans for absence", workaround::PlanExample(prior, Value::Null()).planned.IsNull(), true);
}

void Applying() {
  Section("applying");
  workaround::ProviderSettings settings;
  settings.id_prefix = "wp";
  std::vector<Diagnostic> diagnostics;

  Value planned = workaround::PlanExample(Value::Null(), Example({{"name", Str("web")}, {"size", Num(2)}})).planned;
  Value created = workaround::ApplyExample(Value::Null(), planned, settings, "abc123", &diagnostics);
  Expect("create: the id is prefix-suffix", created.Attr("id").DebugString(), R"("wp-abc123")");
  Expect("create: nothing unknown is left", created.IsWhollyKnown(), true);
  Expect("create: summary", created.Attr("summary").DebugString(), R"("web: size=2, enabled=true")");
  Expect("create: no diagnostics", Describe(diagnostics), "none");

  // What the plan said must come true: every known planned value is unchanged.
  bool honoured = true;
  for (const auto& [name, value] : planned.Entries()) {
    if (value.IsKnown() && value != created.Attr(name)) honoured = false;
  }
  Expect("create: every value the plan made known is unchanged", honoured, true);

  Value updated_plan = workaround::PlanExample(created, Created("wp-abc123", {{"name", Str("web")}, {"size", Num(5)},
                                                                              {"enabled", Value::Bool(true)}})).planned;
  Value updated = workaround::ApplyExample(created, updated_plan, settings, "ignored", &diagnostics);
  Expect("update: the id does not change", updated.Attr("id").DebugString(), R"("wp-abc123")");
  Expect("update: summary", updated.Attr("summary").DebugString(), R"("web: size=5, enabled=true")");

  Expect("destroy returns null", workaround::ApplyExample(created, Value::Null(), settings, "x", &diagnostics).IsNull(), true);

  std::vector<Diagnostic> incomplete;
  Value half = workaround::ApplyExample(Value::Null(), Example({{"name", Str("a")}, {"size", Value::Unknown()},
                                                                {"enabled", Value::Bool(true)}, {"id", Value::Unknown()}}),
                                        settings, "s", &incomplete);
  ExpectNegative("an unknown input at apply time is reported, not hidden", Describe(incomplete), "error: Incomplete plan []");
  Expect("... and the id is still assigned", half.Attr("id").DebugString(), R"("wp-s")");
}

}  // namespace

void RunExampleTests() {
  Schemas();
  ProviderBlock();
  Validation();
  Planning();
  Applying();
}
