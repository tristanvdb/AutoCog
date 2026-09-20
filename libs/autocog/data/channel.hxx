#ifndef AUTOCOG_DATA_CHANNEL_HXX
#define AUTOCOG_DATA_CHANNEL_HXX

#include "autocog/data/expr.hxx"
#include "autocog/data/path.hxx"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace autocog::data {

// Conversion for every type below lives in the free functions in
// json.hxx / python.hxx; these carry identity (hash) only.

// --- Clauses: a transformation pipeline applied in order ---------------------

struct BindClause   { std::vector<PathStep> source; std::vector<PathStep> target; };
struct RavelClause  { std::optional<int> depth; std::vector<PathStep> target; };
struct WrapClause   { std::vector<PathStep> target; };
struct PruneClause  { std::vector<PathStep> target; };
struct MappedClause { std::vector<PathStep> target; };

struct Clause {
  std::variant<BindClause, RavelClause, WrapClause, PruneClause, MappedClause> value;
  std::string hash() const;
};

// --- Call kwarg --------------------------------------------------------------

struct ChannelKwarg {
  std::string name;
  bool is_input = false;
  std::optional<std::string> prompt;   ///< source prompt (null = self)
  std::vector<PathStep> path;
  ///< The argument's VALUE, when it is not read from the document: an
  ///< expression, so a literal (`zero_index is 0`) and a reference to
  ///< something the program cannot know yet (`zero_index use
  ///< __syntax__.prompt_zero_index`) are the same kind of thing rather than
  ///< two fields. Resolved when the call is made, which is after a syntax has
  ///< been chosen.
  std::optional<Expr> value;
  std::vector<Clause> clauses;
  std::string hash() const;
};

// --- Channels ----------------------------------------------------------------

struct InputChannel { std::vector<PathStep> target; std::vector<PathStep> source; };

struct DataflowChannel {
  std::vector<PathStep> target;
  std::optional<std::string> prompt;   ///< source prompt (null = self previous)
  std::vector<PathStep> source;
  std::vector<Clause> clauses;
};

struct CallChannel {
  std::vector<PathStep> target;
  std::optional<std::string> extern_func;  ///< Python callable (null = none)
  std::optional<std::string> entry;        ///< prompt entry point (null = none)
  std::vector<ChannelKwarg> kwargs;
  /// `call f <path>` instead of `call f { ... }`: spread the record at <path>
  /// as the arguments. An empty path is the root. Excludes `kwargs`.
  std::optional<std::vector<PathStep>> spread;
  std::vector<Clause> clauses;
};

struct Channel {
  std::variant<InputChannel, DataflowChannel, CallChannel> value;
  std::string hash() const;
};

}

#endif // AUTOCOG_DATA_CHANNEL_HXX
