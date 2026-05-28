
#include "autocog/runtime/sta/state.hxx"
#include "autocog/runtime/sta/channel.hxx"
#include "autocog/runtime/sta/syntax.hxx"
#include "autocog/utilities/errors.hxx"

#include <nlohmann/json.hpp>
#include <fstream>

namespace autocog::runtime::sta {

Syntax load_syntax(std::string const & path) {
    using json = nlohmann::json;

    std::ifstream f(path);
    if (!f) throw autocog::ConfigError("Cannot read syntax file: " + path, path);
    auto j = json::parse(f);

    auto require_str = [&](char const * key) -> std::string {
        if (!j.contains(key))
            throw autocog::ConfigError(
                std::string("Syntax file missing required field: ") + key, path);
        return j[key].get<std::string>();
    };
    auto require_bool = [&](char const * key) -> bool {
        if (!j.contains(key))
            throw autocog::ConfigError(
                std::string("Syntax file missing required field: ") + key, path);
        return j[key].get<bool>();
    };

    Syntax s;
    s.system_msg       = require_str("system_msg");
    s.header_pre       = require_str("header_pre");
    s.header_mid       = require_str("header_mid");
    s.header_post      = require_str("header_post");
    s.header_mechanic  = require_str("header_mechanic");
    s.header_formats   = require_str("header_formats");
    s.format_listing   = require_str("format_listing");
    s.prompt_indent    = require_str("prompt_indent");
    s.field_separator  = require_str("field_separator");
    s.field_suffix     = require_str("field_suffix");
    s.desc_pre         = require_str("desc_pre");
    s.desc_post        = require_str("desc_post");
    s.desc_inline      = require_bool("desc_inline");
    s.prompt_with_format = require_bool("prompt_with_format");
    s.prompt_with_index  = require_bool("prompt_with_index");
    s.prompt_zero_index  = require_bool("prompt_zero_index");
    s.detailed_formats   = require_bool("detailed_formats");
    s.completion_stop    = require_str("completion_stop");

    return s;
}

}
