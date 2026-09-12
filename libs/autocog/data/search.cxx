#include "autocog/data/search.hxx"

#include "autocog/data/utility.hxx"

namespace autocog::data {

std::string SearchConfig::content_hash() const {
  ContentHasher h;
  h.put(text.threshold).put(text.beams).put(text.topk).put(text.ahead).put(text.width)
   .put(text.repetition).put(text.diversity);
  auto put_choice = [&h](ChoiceSearch const & c) {
    h.put(c.threshold).put(c.width);
    // Non-default only: explicit "mean" hashes identically to absent, so
    // pre-knob configs keep their content ids.
    if (c.ranking != "mean")          h.put(c.ranking);
    if (c.threshold_metric != "mean") h.put(c.threshold_metric);
  };
  put_choice(enums);
  put_choice(branch);
  put_choice(flow);
  h.put(queue.metric);
  h.put(queue.stop ? queue.stop->hash() : std::string{});
  return h.hash();
}

}
