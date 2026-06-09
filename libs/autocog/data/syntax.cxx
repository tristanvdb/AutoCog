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
    .hash();
}

}
