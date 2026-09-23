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

namespace autocog::data {

namespace {

std::optional<Value> choice_get(ChoiceSearch const & c, std::string const & key) {
  if (key == "threshold" || key == "threshold.value") return c.threshold;
  if (key == "threshold.metric") return c.threshold_metric;
  if (key == "ranking.metric")   return c.ranking;
  if (key == "width")            return static_cast<int>(c.width);
  return std::nullopt;
}

}  // namespace

std::optional<Value> search_get(SearchConfig const & cfg,
                                std::string const & category,
                                std::string const & key) {
  if (category == "text") {
    if (key == "threshold")  return cfg.text.threshold;
    if (key == "beams")      return static_cast<int>(cfg.text.beams);
    if (key == "ahead")      return static_cast<int>(cfg.text.ahead);
    if (key == "width")      return static_cast<int>(cfg.text.width);
    if (key == "topk")       return cfg.text.topk ? std::optional<Value>(static_cast<int>(*cfg.text.topk)) : std::nullopt;
    if (key == "repetition") return cfg.text.repetition ? std::optional<Value>(*cfg.text.repetition) : std::nullopt;
    if (key == "diversity")  return cfg.text.diversity ? std::optional<Value>(*cfg.text.diversity) : std::nullopt;
    return std::nullopt;
  }
  if (category == "enum")   return choice_get(cfg.enums, key);
  if (category == "branch") return choice_get(cfg.branch, key);
  if (category == "flow")   return choice_get(cfg.flow, key);
  if (category == "queue") {
    if (key == "metric") {
      // The lexicographic list, comma-joined: the same spelling the STL policy
      // uses for it.
      std::string out;
      for (auto const & m : cfg.queue.metric) { if (!out.empty()) out += ","; out += m; }
      return out;
    }
    return std::nullopt;   // queue.stop is a predicate, not a scalar
  }
  return std::nullopt;
}

}
