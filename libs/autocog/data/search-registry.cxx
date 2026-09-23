#include "autocog/data/search-registry.hxx"

namespace autocog::data::registry {

SearchParam const * find(std::string const & category, std::string const & key) {
  for (auto const & p : SEARCH_PARAMS) {
    if (category == p.category && key == p.key) return &p;
  }
  return nullptr;
}

bool known_category(std::string const & category) {
  for (auto const & p : SEARCH_PARAMS) {
    if (category == p.category) return true;
  }
  return false;
}

bool in_domain(SearchParam const & param, std::string const & value) {
  if (param.type != Type::Str || param.domain == nullptr) return true;
  for (char const * const * v = param.domain; *v != nullptr; ++v) {
    if (value == *v) return true;
  }
  return false;
}

std::string domain_text(SearchParam const & param) {
  std::string out;
  if (param.type != Type::Str || param.domain == nullptr) return out;
  for (char const * const * v = param.domain; *v != nullptr; ++v) {
    if (!out.empty()) out += ", ";
    out += *v;
  }
  return out;
}

}
