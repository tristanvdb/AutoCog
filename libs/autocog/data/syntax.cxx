#include "autocog/data/syntax.hxx"

#include "autocog/data/utility.hxx"

namespace autocog::data {

std::string Syntax::content_hash() const {
  return ContentHasher{}
    .put(system_msg)
    .put(header_pre)
    .put(header_mid)
    .put(header_post)
    .put(header_mechanic)
    .put(header_formats)
    .put(format_listing)
    .put(prompt_indent)
    .put(field_separator)
    .put(field_suffix)
    .put(desc_pre)
    .put(desc_post)
    .put(desc_inline)
    .put(prompt_with_format)
    .put(prompt_with_index)
    .put(prompt_zero_index)
    .put(detailed_formats)
    .put(completion_stop)
    .put(completion_vocab ? std::optional<std::string>(completion_vocab->hash()) : std::nullopt)
    .hash();
}

}

namespace autocog::data {

std::optional<Value> syntax_get(Syntax const & s, std::string const & f) {
  // The table is the one in namespaces.cxx; keep them together when a knob is
  // added. A knob that is not here is not nameable, and the registry says so
  // before an expression ever reaches this point.
  if (f == "system_msg")         return s.system_msg;
  if (f == "header_pre")         return s.header_pre;
  if (f == "header_mid")         return s.header_mid;
  if (f == "header_post")        return s.header_post;
  if (f == "header_mechanic")    return s.header_mechanic;
  if (f == "header_formats")     return s.header_formats;
  if (f == "format_listing")     return s.format_listing;
  if (f == "prompt_indent")      return s.prompt_indent;
  if (f == "field_separator")    return s.field_separator;
  if (f == "field_suffix")       return s.field_suffix;
  if (f == "desc_pre")           return s.desc_pre;
  if (f == "desc_post")          return s.desc_post;
  if (f == "desc_inline")        return s.desc_inline;
  if (f == "prompt_with_format") return s.prompt_with_format;
  if (f == "prompt_with_index")  return s.prompt_with_index;
  if (f == "prompt_zero_index")  return s.prompt_zero_index;
  if (f == "detailed_formats")   return s.detailed_formats;
  if (f == "completion_stop")    return s.completion_stop;
  return std::nullopt;
}

}
