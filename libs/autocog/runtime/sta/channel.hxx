#ifndef AUTOCOG_RUNTIME_STA_CHANNEL_HXX
#define AUTOCOG_RUNTIME_STA_CHANNEL_HXX

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "autocog/runtime/sta/path.hxx"

namespace autocog::runtime::sta {

// ============================================================================
// Clauses — transformation pipeline applied in parse order
// ============================================================================

struct BindClause {
    std::vector<PathStep> source;
    std::vector<PathStep> target;  // empty = apply to root
};

struct RavelClause {
    int depth = 1;
    std::vector<PathStep> target;  // empty = apply to root
};

struct WrapClause {
    std::vector<PathStep> target;  // empty = apply to root
};

struct PruneClause {
    std::vector<PathStep> target;
};

struct MappedClause {
    std::vector<PathStep> target;  // empty = map entire value
};

using Clause = std::variant<BindClause, RavelClause, WrapClause, PruneClause, MappedClause>;

// ============================================================================
// Kwarg — argument to a call channel
// ============================================================================

struct ChannelKwarg {
    std::string name;
    bool is_input;                          // true = from external inputs, false = dataflow
    std::optional<std::string> prompt;      // source prompt (nullopt = self)
    std::vector<PathStep> path;             // source path
    std::optional<std::string> value;       // literal value (from "is" keyword)
    std::vector<Clause> clauses;            // transformation pipeline
};

// ============================================================================
// Channel types
// ============================================================================

struct InputChannel {
    std::vector<PathStep> target;
    std::vector<PathStep> source;
};

struct DataflowChannel {
    std::vector<PathStep> target;
    std::optional<std::string> prompt;      // source prompt (nullopt = self previous)
    std::vector<PathStep> source;
    std::vector<Clause> clauses;
};

struct CallChannel {
    std::vector<PathStep> target;
    std::optional<std::string> extern_func; // Python callable
    std::optional<std::string> entry;       // prompt entry point
    std::vector<ChannelKwarg> kwargs;
    std::vector<Clause> clauses;            // applied to call result
};

using Channel = std::variant<InputChannel, DataflowChannel, CallChannel>;

}

#endif // AUTOCOG_RUNTIME_STA_CHANNEL_HXX
