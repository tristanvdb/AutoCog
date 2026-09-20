#include "autocog/data/namespaces.hxx"
#include "autocog/data/search-registry.hxx"

#include <vector>

namespace autocog::data::namespaces {

namespace {

struct FieldDecl {
  char const * ns;
  char const * name;
  FieldType type;
  Stage stage;
};

// __syntax__ mirrors data::Syntax. completion_vocab is deliberately absent: it
// is a set, and the expression language carries scalars.
constexpr FieldDecl FIELDS[] = {
  {"__syntax__", "system_msg",         FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "header_pre",         FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "header_mid",         FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "header_post",        FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "header_mechanic",    FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "header_formats",     FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "format_listing",     FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "prompt_indent",      FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "field_separator",    FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "field_suffix",       FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "desc_pre",           FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "desc_post",          FieldType::Str,  Stage::Instantiate},
  {"__syntax__", "desc_inline",        FieldType::Bool, Stage::Instantiate},
  {"__syntax__", "prompt_with_format", FieldType::Bool, Stage::Instantiate},
  {"__syntax__", "prompt_with_index",  FieldType::Bool, Stage::Instantiate},
  {"__syntax__", "prompt_zero_index",  FieldType::Bool, Stage::Instantiate},
  {"__syntax__", "detailed_formats",   FieldType::Bool, Stage::Instantiate},
  {"__syntax__", "completion_stop",    FieldType::Str,  Stage::Instantiate},

  // Properties of the bound model. Folded when the evaluation is set up, so
  // the hot loop only ever compares numbers.
  {"__model__",  "n_ctx",              FieldType::Int,  Stage::Execute},

  // Statistics of the running evaluation -- the scalars a termination
  // predicate asks about. Wall time is deliberately not among them: it would
  // make a run nondeterministic, and termination is part of program identity.
  {"__status__", "terminals",          FieldType::Float, Stage::Execute},
  {"__status__", "coverage.fta",       FieldType::Float, Stage::Execute},
  {"__status__", "coverage.sta",       FieldType::Float, Stage::Execute},
  {"__status__", "best.proba",         FieldType::Float, Stage::Execute},
  {"__status__", "mean.proba",         FieldType::Float, Stage::Execute},
  {"__status__", "std.proba",          FieldType::Float, Stage::Execute},
  {"__status__", "best.zscore",        FieldType::Float, Stage::Execute},
  {"__status__", "tokens",             FieldType::Float, Stage::Execute},
  {"__status__", "queue.size",         FieldType::Float, Stage::Execute},
};

constexpr char const * NAMESPACES[] = {
  "__syntax__", "__search__", "__program__", "__model__", "__status__",
};

FieldDecl const * find_field(std::string const & ns, std::string const & field) {
  for (auto const & f : FIELDS)
    if (ns == f.ns && field == f.name) return &f;
  return nullptr;
}

bool is_known_namespace(std::string const & ns) {
  for (auto const * n : NAMESPACES) if (ns == n) return true;
  return false;
}

/// Split "__search__.enum.width" into ("__search__", "enum.width").
std::pair<std::string, std::string> split(std::string const & ref) {
  auto const dot = ref.find('.');
  if (dot == std::string::npos) return {ref, std::string{}};
  return {ref.substr(0, dot), ref.substr(dot + 1)};
}

/// __search__ delegates to the search-parameter registry: its field set IS the
/// set of parameters, and duplicating it here is how the two drift.
registry::SearchParam const * find_search(std::string const & field) {
  auto const dot = field.rfind('.');
  if (dot == std::string::npos) return nullptr;
  // "enum.threshold.value" -> category "enum", key "threshold.value".
  auto const first = field.find('.');
  return registry::find(field.substr(0, first), field.substr(first + 1));
}

FieldType search_type(registry::SearchParam const & p) {
  switch (p.type) {
    case registry::Type::Float: return FieldType::Float;
    case registry::Type::UInt:  return FieldType::Int;
    case registry::Type::Str:   return FieldType::Str;
  }
  return FieldType::Str;
}

/// Resolve a reference to its declaration, or throw the diagnostic that says
/// exactly what is wrong with it.
struct Resolved {
  FieldType type;
  Stage stage;
};

Resolved resolve(std::string const & ref,
                 std::optional<autocog::location::SourceRange> const & loc) {
  auto const [ns, field] = split(ref);

  if (!is_namespace_word(ns))
    throw CompileError("'" + ns + "' is not a namespace root (dotted names "
                       "resolve against system namespaces)", loc);

  if (!is_known_namespace(ns))
    throw CompileError("unknown system namespace '" + ns + "'", loc);

  if (field.empty())
    throw CompileError("'" + ns + "' is a namespace, not a value: name a field "
                       "of it (" + fields_of(ns) + ")", loc);

  if (ns == "__program__")
    throw CompileError("system namespace '__program__' is reserved but has no "
                       "fields yet", loc);

  if (ns == "__search__") {
    auto const * p = find_search(field);
    if (!p)
      throw CompileError("'" + ref + "' is not a search parameter", loc);
    return {search_type(*p), Stage::Instantiate};
  }

  auto const * f = find_field(ns, field);
  if (!f)
    throw CompileError("'" + ns + "' has no field '" + field + "' (has: "
                       + fields_of(ns) + ")", loc);
  return {f->type, f->stage};
}

}  // namespace

char const * type_word(FieldType t) {
  switch (t) {
    case FieldType::Bool:  return "bool";
    case FieldType::Int:   return "int";
    case FieldType::Float: return "float";
    case FieldType::Str:   return "string";
  }
  return "?";
}

bool is_namespace_word(std::string const & w) {
  return w.size() >= 4 && w.compare(0, 2, "__") == 0
      && w.compare(w.size() - 2, 2, "__") == 0;
}

std::string root_of(std::string const & ref) {
  auto const [ns, field] = split(ref);
  return is_namespace_word(ns) ? ns : std::string{};
}

bool known(std::string const & ref) {
  try {
    resolve(ref, std::nullopt);
    return true;
  } catch (CompileError const &) {
    return false;
  }
}

Stage stage_of(std::string const & ref,
               std::optional<autocog::location::SourceRange> const & loc) {
  return resolve(ref, loc).stage;
}

FieldType type_of(std::string const & ref,
                  std::optional<autocog::location::SourceRange> const & loc) {
  return resolve(ref, loc).type;
}

void check(std::string const & ref,
           std::optional<autocog::location::SourceRange> const & loc) {
  resolve(ref, loc);
}

std::string qualify(std::string const & bare) {
  if (!root_of(bare).empty()) return bare;               // already qualified
  if (bare.rfind("model.", 0) == 0)
    return "__model__." + bare.substr(6);
  if (find_field("__status__", bare)) return "__status__." + bare;
  return bare;
}

std::string fields_of(std::string const & ns) {
  if (ns == "__search__") return "any search parameter, as <category>.<key>";
  std::string out;
  for (auto const & f : FIELDS)
    if (ns == f.ns) { if (!out.empty()) out += ", "; out += f.name; }
  return out.empty() ? "none" : out;
}

}
