#ifndef AUTOCOG_DATA_SYNTAX_HXX
#define AUTOCOG_DATA_SYNTAX_HXX

#include "autocog/data/base.hxx"

#include <string>

namespace autocog::data {

/// Syntax configuration: the formatting/templating knobs used when rendering
/// prompts. A flat set of string/bool fields. Conversion lives in the free
/// functions in json.hxx / python.hxx.
class Syntax : public Base<Syntax> {
  public:
    static constexpr char const * format = "syntax";

    std::string system_msg;

    std::string header_pre;
    std::string header_mid;
    std::string header_post;
    std::string header_mechanic;
    std::string header_formats;
    std::string format_listing;

    std::string prompt_indent;
    std::string field_separator;
    std::string field_suffix;

    std::string desc_pre;
    std::string desc_post;
    bool desc_inline = false;

    bool prompt_with_format = false;
    bool prompt_with_index = false;
    bool prompt_zero_index = false;

    bool detailed_formats = false;

    std::string completion_stop;

  public:
    std::string content_hash() const;
};

}

#endif // AUTOCOG_DATA_SYNTAX_HXX
