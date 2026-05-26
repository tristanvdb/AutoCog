#ifndef AUTOCOG_RUNTIME_STA_STATE_HXX
#define AUTOCOG_RUNTIME_STA_STATE_HXX

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "autocog/runtime/sta/channel.hxx"

namespace autocog::runtime::sta {

// ============================================================================
// Field format descriptions (self-contained, no IR dependency)
// ============================================================================

struct CompletionFormat {
    std::optional<int> length;
    std::optional<float> threshold;
    std::optional<int> beams;
    std::optional<int> ahead;
    std::optional<int> width;
};

struct EnumFormat {
    std::vector<std::string> values;
    std::optional<int> width;
};

struct ChoiceFormat {
    std::string mode;   // "select" or "repeat"
    std::vector<std::pair<std::string, std::optional<std::pair<int,int>>>> path;
    std::optional<int> width;
};

using FieldFormat = std::variant<
    std::monostate,     // record (no format)
    CompletionFormat,
    EnumFormat,
    ChoiceFormat
>;

// ============================================================================
// Per-field info carried in the STA
// ============================================================================

struct FieldInfo {
    std::string name;
    int depth;
    int index;        // index within parent
    int flat_index;   // position in flattened field list (globally unique)
    std::optional<std::pair<int,int>> range;  // null = scalar
    FieldFormat format;
    std::vector<std::string> desc;

    bool is_list() const { return range.has_value(); }
    bool is_record() const { return std::holds_alternative<std::monostate>(format); }

    std::string tag() const {
        return name + "_" + std::to_string(depth) + "_" + std::to_string(flat_index);
    }
};

// ============================================================================
// Concrete state in the automaton
// ============================================================================

struct ConcreteState {
    std::string tag;            // unique id: "field_tag@i_j_k"
    int field;                  // index into PromptSTA::fields (-1 for root)
    std::vector<int> indices;   // concrete array indices (depth-0 omitted)
    std::vector<std::string> flows;      // flow targets (before post-processing)
    std::vector<std::string> exits;      // exit targets (before post-processing)
    std::vector<std::string> successors; // final linearized successors
};

// ============================================================================
// Flow control
// ============================================================================

struct FlowTarget {
    std::string prompt;
    int limit;
};

struct ReturnField {
    std::string alias;
    std::vector<std::pair<std::string, std::optional<int>>> path;
};

struct ReturnTarget {
    std::vector<ReturnField> fields;
};

using FlowEntry = std::variant<FlowTarget, ReturnTarget>;

// ============================================================================
// Per-prompt STA
// ============================================================================

struct PromptSTA {
    std::string name;
    std::vector<std::string> desc;
    std::vector<FieldInfo> fields;
    std::map<std::string, ConcreteState> states;    // tag -> state
    std::vector<std::string> sequence;              // linear traversal order
    std::map<std::string, FlowEntry> flows;         // name -> target
    std::vector<Channel> channels;                  // data flow descriptions
};

// ============================================================================
// Full STA program
// ============================================================================

struct PythonImport {
    std::string file;
    std::string target;
};

struct Program {
    std::map<std::string, std::string> entry_points;
    std::map<std::string, PromptSTA> prompts;
    std::map<std::string, PythonImport> python_imports;
};

}

#endif // AUTOCOG_RUNTIME_STA_STATE_HXX
