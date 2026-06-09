#ifndef AUTOCOG_RUNTIME_STA_STATE_HXX
#define AUTOCOG_RUNTIME_STA_STATE_HXX

#include <string>
#include <vector>

#include "autocog/data/sta.hxx"   // STA/Prompt/Field (+ formats, flow, abstract, ...)

namespace autocog::runtime::sta {

// The STA's structural types are the shared autocog::data types; the runtime
// works on them directly (data::Field carries the structural query helpers
// is_list/is_record/tag). The only runtime-only type is ConcreteState below:
// the index-expanded automaton state built during instantiation, never
// serialized. A few short aliases are re-exported for readability.
using SearchParams     = ::autocog::data::SearchParams;
using CompletionFormat = ::autocog::data::CompletionFormat;
using EnumFormat       = ::autocog::data::EnumFormat;
using ChoiceFormat     = ::autocog::data::ChoiceFormat;
using FieldFormat      = ::autocog::data::FieldFormat;
using FlowControl      = ::autocog::data::FlowControl;
using FlowReturn       = ::autocog::data::FlowReturn;
using ReturnField      = ::autocog::data::ReturnField;
using Flow             = ::autocog::data::Flow;
using AbstractState    = ::autocog::data::Abstract;
using SchemaField      = ::autocog::data::SchemaField;
using EntryPoint       = ::autocog::data::EntryPoint;
using PythonImport     = ::autocog::data::PythonImport;
using VocabExpr        = ::autocog::data::VocabExpr;

// ============================================================================
// Concrete state in the automaton (runtime scratch; NOT serialized)
// ============================================================================
struct ConcreteState {
    std::string tag;            // unique id: "field_tag@i_j_k"
    int field;                  // index into the prompt's fields (-1 for root)
    std::vector<int> indices;   // concrete array indices (depth-0 omitted)
    std::vector<std::string> flows;      // flow targets (before post-processing)
    std::vector<std::string> exits;      // exit targets (before post-processing)
    std::vector<std::string> successors; // final linearized successors
};

// Depth/tag of an abstract state, derived from the field table (root = depth 0,
// tag "root").
inline int abs_depth(AbstractState const & abs,
                     std::vector<::autocog::data::Field> const & fields) {
    return (abs.field < 0) ? 0 : fields[abs.field].depth;
}
inline std::string abs_tag(AbstractState const & abs,
                           std::vector<::autocog::data::Field> const & fields) {
    return (abs.field < 0) ? "root" : fields[abs.field].tag();
}

}

#endif // AUTOCOG_RUNTIME_STA_STATE_HXX
