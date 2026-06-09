#include "autocog/codec/python.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <>
pybind11::object to_py(Syntax const & s) {
  namespace py = pybind11;
  py::dict d;
  d["system_msg"]         = s.system_msg;
  d["header_pre"]         = s.header_pre;
  d["header_mid"]         = s.header_mid;
  d["header_post"]        = s.header_post;
  d["header_mechanic"]    = s.header_mechanic;
  d["header_formats"]     = s.header_formats;
  d["format_listing"]     = s.format_listing;
  d["prompt_indent"]      = s.prompt_indent;
  d["field_separator"]    = s.field_separator;
  d["field_suffix"]       = s.field_suffix;
  d["desc_pre"]           = s.desc_pre;
  d["desc_post"]          = s.desc_post;
  d["desc_inline"]        = s.desc_inline;
  d["prompt_with_format"] = s.prompt_with_format;
  d["prompt_with_index"]  = s.prompt_with_index;
  d["prompt_zero_index"]  = s.prompt_zero_index;
  d["detailed_formats"]   = s.detailed_formats;
  d["completion_stop"]    = s.completion_stop;
  if (s.metadata) d["metadata"] = to_py(*s.metadata);
  d["provenance"] = s.provenance;
  return d;
}

template <>
void from_py(pybind11::object const & obj, Syntax & s) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  s.system_msg         = d["system_msg"].cast<std::string>();
  s.header_pre         = d["header_pre"].cast<std::string>();
  s.header_mid         = d["header_mid"].cast<std::string>();
  s.header_post        = d["header_post"].cast<std::string>();
  s.header_mechanic    = d["header_mechanic"].cast<std::string>();
  s.header_formats     = d["header_formats"].cast<std::string>();
  s.format_listing     = d["format_listing"].cast<std::string>();
  s.prompt_indent      = d["prompt_indent"].cast<std::string>();
  s.field_separator    = d["field_separator"].cast<std::string>();
  s.field_suffix       = d["field_suffix"].cast<std::string>();
  s.desc_pre           = d["desc_pre"].cast<std::string>();
  s.desc_post          = d["desc_post"].cast<std::string>();
  s.desc_inline        = d["desc_inline"].cast<bool>();
  s.prompt_with_format = d["prompt_with_format"].cast<bool>();
  s.prompt_with_index  = d["prompt_with_index"].cast<bool>();
  s.prompt_zero_index  = d["prompt_zero_index"].cast<bool>();
  s.detailed_formats   = d["detailed_formats"].cast<bool>();
  s.completion_stop    = d["completion_stop"].cast<std::string>();
  if (d.contains("metadata")) { s.metadata.emplace(); from_py(d["metadata"], *s.metadata); }
  if (d.contains("provenance"))
    s.provenance = d["provenance"].cast<std::map<std::string, std::string>>();
}

}
