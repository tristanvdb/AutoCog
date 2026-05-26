#ifndef AUTOCOG_RUNTIME_STA_PARSE_HXX
#define AUTOCOG_RUNTIME_STA_PARSE_HXX

#include "autocog/runtime/sta/state.hxx"
#include "autocog/runtime/sta/syntax.hxx"
#include "autocog/runtime/sta/value.hxx"

#include <nlohmann/json.hpp>

namespace autocog::runtime::sta {

// Parse generated text back into a FieldRecord.
// The text follows the syntax format: labels, separators, indentation.
// Returns a flat record mapping field names to values (including "next" for flow).
// If content is provided, select-mode choices are resolved back to values.
FieldRecord parse_text(
    PromptSTA const & prompt,
    Syntax const & syntax,
    std::string const & text,
    nlohmann::json const * content = nullptr
);

// Convert FieldValue to nlohmann::json for serialization
nlohmann::json field_value_to_json(FieldValue const & val);
nlohmann::json field_record_to_json(FieldRecord const & rec);

}

#endif // AUTOCOG_RUNTIME_STA_PARSE_HXX
