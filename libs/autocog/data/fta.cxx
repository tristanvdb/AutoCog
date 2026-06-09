#include "autocog/data/fta.hxx"

#include "autocog/data/utility.hxx"

namespace autocog::data {

namespace {
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace

std::string Action::hash() const {
  ContentHasher h;
  h.put(uid);
  h.put(static_cast<unsigned>(body.index()));
  std::visit(overloaded{
    [&](TextAction const & a) {
      h.put(a.text).put(a.evaluate);
    },
    [&](CompleteAction const & a) {
      h.put(a.length).put(a.threshold).put(a.beams).put(a.ahead).put(a.width)
       .put(a.stop_text).put(a.repetition).put(a.diversity).put(a.vocab);
    },
    [&](ChooseAction const & a) {
      h.put(a.choices).put(a.threshold).put(a.width);
    },
  }, body);
  h.put(field).put(indices).put(successors);
  return h.hash();
}

std::string FTA::content_hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(actions.size()));
  for (auto const & a : actions) h.put(a.hash());   // positional: actions[0] is the entry
  h.put(queue_metric);
  h.put(static_cast<unsigned>(vocabs.size()));
  for (auto const & [k, ve] : vocabs) h.put(k).put(ve.hash());  // std::map: canonical by key
  return h.hash();
}

}
