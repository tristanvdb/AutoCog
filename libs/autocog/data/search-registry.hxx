#ifndef AUTOCOG_DATA_SEARCH_REGISTRY_HXX
#define AUTOCOG_DATA_SEARCH_REGISTRY_HXX

#include <string>

namespace autocog::data::registry {

/// The search-parameter registry: the single source of truth for every
/// parameter a `search { }` block or a search config may set. Consumed by
///  - the STL compiler (`lower_search`, stage 5): unknown key / wrong type /
///    out-of-domain value is a compile error, not a silently dropped policy;
///  - the JSON/python codecs: the same validation for search config files;
///  - the runtime resolver (`ista`): typed reads of per-field policies.
/// The published JSON schema duplicates this list; that duplication is
/// accepted (generating the schema from this table is out of scope).

enum class Kind { Scalar, List, Predicate };
enum class Type { Float, UInt, Str };

/// One parameter. `domain` is a null-terminated array of allowed string
/// values (Str only; nullptr = unrestricted). `nullable` marks parameters
/// whose value may be null/unset (optional semantics). `since` is the
/// version the parameter was introduced in (used by error messages).
struct SearchParam {
  char const * category;  ///< "text" | "enum" | "branch" | "flow" | "queue"
  char const * key;       ///< dotted key, e.g. "threshold.metric"
  Kind kind;
  Type type;
  char const * const * domain;
  bool nullable;
  char const * since;
};

/// Candidate-scoring metrics for choice actions (ranking.metric and
/// threshold.metric): per-token geometric mean, joint log-probability,
/// per-byte normalization.
inline constexpr char const * SCORE_METRICS[] = {"mean", "sum", "bytes", nullptr};

/// Queue-ordering metrics (lexicographic lists; must match the backend's
/// parse_metric_key).
inline constexpr char const * QUEUE_METRICS[] = {
    "perplexity", "probability", "shortest", "longest",
    "shallowest", "deepest", "near_leaf", "far_leaf", "fifo", nullptr};

// The three choice categories (enum/branch/flow) share the same shape:
// bare "threshold" is accepted as shorthand for "threshold.value".
#define AUTOCOG_CHOICE_PARAMS(cat)                                            \
  {cat, "threshold",        Kind::Scalar, Type::Float, nullptr,       false, "0.5"}, \
  {cat, "threshold.value",  Kind::Scalar, Type::Float, nullptr,       false, "0.8"}, \
  {cat, "threshold.metric", Kind::Scalar, Type::Str,   SCORE_METRICS, false, "0.8"}, \
  {cat, "ranking.metric",   Kind::Scalar, Type::Str,   SCORE_METRICS, false, "0.8"}, \
  {cat, "width",            Kind::Scalar, Type::UInt,  nullptr,       false, "0.5"}

inline constexpr SearchParam SEARCH_PARAMS[] = {
  {"text", "threshold",  Kind::Scalar, Type::Float, nullptr, false, "0.5"},
  {"text", "beams",      Kind::Scalar, Type::UInt,  nullptr, false, "0.5"},
  {"text", "topk",       Kind::Scalar, Type::UInt,  nullptr, true,  "0.7"},
  {"text", "ahead",      Kind::Scalar, Type::UInt,  nullptr, false, "0.5"},
  {"text", "width",      Kind::Scalar, Type::UInt,  nullptr, false, "0.5"},
  {"text", "repetition", Kind::Scalar, Type::Float, nullptr, true,  "0.5"},
  {"text", "diversity",  Kind::Scalar, Type::Float, nullptr, true,  "0.5"},
  AUTOCOG_CHOICE_PARAMS("enum"),
  AUTOCOG_CHOICE_PARAMS("branch"),
  AUTOCOG_CHOICE_PARAMS("flow"),
  {"queue", "metric", Kind::List,      Type::Str, QUEUE_METRICS, false, "0.5"},
  {"queue", "stop",   Kind::Predicate, Type::Str, nullptr,       true,  "0.8"},
};

#undef AUTOCOG_CHOICE_PARAMS

/// Look up a parameter; nullptr when the (category, key) pair is unknown.
SearchParam const * find(std::string const & category, std::string const & key);

/// True when `category` is a known category name.
bool known_category(std::string const & category);

/// Str-typed domain check; true for non-Str params or unrestricted domains.
bool in_domain(SearchParam const & param, std::string const & value);

/// Allowed values as "a, b, c" for error messages (empty if unrestricted).
std::string domain_text(SearchParam const & param);

}

#endif // AUTOCOG_DATA_SEARCH_REGISTRY_HXX
