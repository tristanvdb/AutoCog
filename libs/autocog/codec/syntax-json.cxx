#include "autocog/codec/json.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <>
nlohmann::json to_json(Syntax const & s) {
  nlohmann::json j{
    {"system_msg",         s.system_msg},
    {"header_pre",         s.header_pre},
    {"header_mid",         s.header_mid},
    {"header_post",        s.header_post},
    {"header_mechanic",    s.header_mechanic},
    {"header_formats",     s.header_formats},
    {"format_listing",     s.format_listing},
    {"prompt_indent",      s.prompt_indent},
    {"field_separator",    s.field_separator},
    {"field_suffix",       s.field_suffix},
    {"desc_pre",           s.desc_pre},
    {"desc_post",          s.desc_post},
    {"desc_inline",        s.desc_inline},
    {"prompt_with_format", s.prompt_with_format},
    {"prompt_with_index",  s.prompt_with_index},
    {"prompt_zero_index",  s.prompt_zero_index},
    {"detailed_formats",   s.detailed_formats},
    {"completion_stop",    s.completion_stop},
  };
  if (s.metadata) j["metadata"] = to_json(*s.metadata);
  j["provenance"] = s.provenance;
  return j;
}

template <>
void from_json(nlohmann::json const & dom, Syntax & s) {
  autocog::codec::read_guarded("Syntax", [&]{
  s.system_msg         = dom.at("system_msg").get<std::string>();
  s.header_pre         = dom.at("header_pre").get<std::string>();
  s.header_mid         = dom.at("header_mid").get<std::string>();
  s.header_post        = dom.at("header_post").get<std::string>();
  s.header_mechanic    = dom.at("header_mechanic").get<std::string>();
  s.header_formats     = dom.at("header_formats").get<std::string>();
  s.format_listing     = dom.at("format_listing").get<std::string>();
  s.prompt_indent      = dom.at("prompt_indent").get<std::string>();
  s.field_separator    = dom.at("field_separator").get<std::string>();
  s.field_suffix       = dom.at("field_suffix").get<std::string>();
  s.desc_pre           = dom.at("desc_pre").get<std::string>();
  s.desc_post          = dom.at("desc_post").get<std::string>();
  s.desc_inline        = dom.at("desc_inline").get<bool>();
  s.prompt_with_format = dom.at("prompt_with_format").get<bool>();
  s.prompt_with_index  = dom.at("prompt_with_index").get<bool>();
  s.prompt_zero_index  = dom.at("prompt_zero_index").get<bool>();
  s.detailed_formats   = dom.at("detailed_formats").get<bool>();
  s.completion_stop    = dom.at("completion_stop").get<std::string>();
  if (dom.contains("metadata")) { s.metadata.emplace(); from_json(dom.at("metadata"), *s.metadata); }
  if (dom.contains("provenance"))
    s.provenance = dom.at("provenance").get<std::map<std::string, std::string>>();
  });
}

}
