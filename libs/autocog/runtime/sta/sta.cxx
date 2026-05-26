
#include "autocog/runtime/sta/state.hxx"
#include "autocog/runtime/sta/channel.hxx"
#include "autocog/runtime/sta/syntax.hxx"
#include "autocog/runtime/sta/value.hxx"

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace autocog::runtime::sta {

// FieldValue copy operations
FieldValue::FieldValue(FieldValue const & other) {
    if (other.is_string()) {
        data = other.as_string();
    } else if (other.is_record()) {
        data = std::make_unique<FieldRecord>(other.as_record());
    } else if (other.is_array()) {
        data = std::make_unique<FieldArray>(other.as_array());
    }
}

FieldValue & FieldValue::operator=(FieldValue const & other) {
    if (this != &other) {
        if (other.is_string()) {
            data = other.as_string();
        } else if (other.is_record()) {
            data = std::make_unique<FieldRecord>(other.as_record());
        } else if (other.is_array()) {
            data = std::make_unique<FieldArray>(other.as_array());
        }
    }
    return *this;
}

Syntax load_syntax(std::string const & path) {
    using json = nlohmann::json;

    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot read syntax file: " + path);
    auto j = json::parse(f);

    Syntax s;
    if (j.contains("system_msg"))       s.system_msg       = j["system_msg"];
    if (j.contains("header_pre"))       s.header_pre       = j["header_pre"];
    if (j.contains("header_mid"))       s.header_mid       = j["header_mid"];
    if (j.contains("header_post"))      s.header_post      = j["header_post"];
    if (j.contains("header_mechanic"))  s.header_mechanic  = j["header_mechanic"];
    if (j.contains("header_formats"))   s.header_formats   = j["header_formats"];
    if (j.contains("format_listing"))   s.format_listing   = j["format_listing"];
    if (j.contains("prompt_indent"))    s.prompt_indent    = j["prompt_indent"];
    if (j.contains("field_separator"))  s.field_separator  = j["field_separator"];
    if (j.contains("field_suffix"))     s.field_suffix     = j["field_suffix"];
    if (j.contains("prompt_with_format")) s.prompt_with_format = j["prompt_with_format"];
    if (j.contains("prompt_with_index"))  s.prompt_with_index  = j["prompt_with_index"];
    if (j.contains("prompt_zero_index"))  s.prompt_zero_index  = j["prompt_zero_index"];
    if (j.contains("detailed_formats"))   s.detailed_formats   = j["detailed_formats"];
    if (j.contains("completion_stop"))    s.completion_stop     = j["completion_stop"];
    if (j.contains("desc_pre"))          s.desc_pre           = j["desc_pre"];
    if (j.contains("desc_post"))         s.desc_post          = j["desc_post"];
    if (j.contains("desc_inline"))       s.desc_inline        = j["desc_inline"];

    return s;
}

}
