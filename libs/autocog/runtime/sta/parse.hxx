#ifndef AUTOCOG_RUNTIME_STA_PARSE_HXX
#define AUTOCOG_RUNTIME_STA_PARSE_HXX

#include "autocog/runtime/sta/value.hxx"

#include <nlohmann/json.hpp>

namespace autocog::runtime::sta {

// Convert FieldValue to nlohmann::json for serialization
nlohmann::json field_value_to_json(FieldValue const & val);
nlohmann::json field_record_to_json(FieldRecord const & rec);

}

#endif // AUTOCOG_RUNTIME_STA_PARSE_HXX
