#include "autocog/data/path.hxx"

#include "autocog/data/utility.hxx"

#include <cstdint>

namespace autocog::data {

std::string PathStep::hash() const {
  ContentHasher h;
  h.put(name);
  if (!selector) {
    h.put(0u);
  } else if (std::holds_alternative<int>(*selector)) {
    h.put(1u);
    h.put(static_cast<std::int32_t>(std::get<int>(*selector)));
  } else {
    auto const & r = std::get<StepRange>(*selector);
    h.put(2u);
    h.put(r.lower);
    h.put(r.upper);
  }
  return h.hash();
}

}
