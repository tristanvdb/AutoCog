#include "autocog/data/search.hxx"

#include "autocog/data/utility.hxx"

namespace autocog::data {

std::string SearchConfig::content_hash() const {
  ContentHasher h;
  h.put(text.threshold).put(text.beams).put(text.ahead).put(text.width)
   .put(text.repetition).put(text.diversity);
  h.put(enums.threshold).put(enums.width);
  h.put(branch.threshold).put(branch.width);
  h.put(flow.threshold).put(flow.width);
  h.put(queue.metric);
  return h.hash();
}

}
