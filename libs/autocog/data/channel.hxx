#ifndef AUTOCOG_DATA_CHANNEL_HXX
#define AUTOCOG_DATA_CHANNEL_HXX

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
  std::optional<std::string> value;    ///< literal value (null = none)
  ///< dotted system reference, e.g. "__syntax__.prompt_zero_index" -- read at
  ///< call time, because a syntax is chosen at instantiation and a compiled
  ///< program cannot know it
  std::optional<std::string> system;
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
