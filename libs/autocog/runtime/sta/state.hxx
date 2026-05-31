#ifndef AUTOCOG_RUNTIME_STA_STATE_HXX
#define AUTOCOG_RUNTIME_STA_STATE_HXX

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "autocog/runtime/sta/channel.hxx"

namespace autocog::runtime::sta {

// Scalar value carried in opaque search-param dicts (mirrors the compiler IR's
// value variant). The search dicts are intentionally open; values are
// resolved/validated downstream at instantiation.
using SearchValue = std::variant<int, float, bool, std::string, std::nullptr_t>;
using SearchParams = std::map<std::string, std::map<std::string, SearchValue>>;

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
    std::optional<std::string> format_ref;  // named format/record (e.g. "thought")
    std::vector<std::string> desc;           // field annotations (from prompt)
    std::vector<std::string> format_desc;    // format annotations (from record definition)

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
    std::optional<int> limit;
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
// Input/Output Schema
// ============================================================================

struct SchemaField {
    std::string type;                        // "text", "array", "object"
    bool required{true};
    std::optional<int> max_length;           // for text fields
    std::vector<std::string> enum_values;    // for enum fields
    // For arrays:
    std::string items_type;                  // element type (for arrays)
    std::optional<int> items_max_length;     // element max_length (for arrays)
    std::optional<int> length;               // fixed array length
    std::optional<int> min_items;
    std::optional<int> max_items;
};

struct EntryPoint {
    std::string prompt;
    std::map<std::string, SchemaField> inputs;
    std::map<std::string, SchemaField> outputs;
};

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
    // Prompt-scope search params from `search { }`, carried verbatim from the
    // IR (category -> param -> value), OPEN by design. Per-field (completion/
    // choice) and per-state (branch) resolution is applied at instantiation;
    // queue.* is meaningful at this prompt scope. Not yet flattened onto fields/
    // states — that scope cascade is a later step.
    std::map<std::string, std::map<std::string, SearchValue>> search;
};

// ============================================================================
// Full STA program
// ============================================================================

struct PythonImport {
    std::string file;
    std::string target;
};

struct Program {
    std::map<std::string, EntryPoint> entry_points;
    std::map<std::string, PromptSTA> prompts;
    std::map<std::string, PythonImport> python_imports;
};

}

#endif // AUTOCOG_RUNTIME_STA_STATE_HXX
