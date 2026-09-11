#include "autocog/backend/llama/prepared.hxx"
#include "autocog/backend/llama/manager.hxx"
#include "autocog/backend/llama/model.hxx"

#include "autocog/utilities/errors.hxx"

#include <chrono>
#include <functional>
#include <map>
#include <variant>

namespace autocog::backend::llama {

PreparedFTA prepare(ModelID const id, data::FTA const & fta) {
  auto const t0 = std::chrono::steady_clock::now();
  Model & model = Manager::get_model(id);

  std::map<std::string, unsigned> uid_to_index;
  for (unsigned i = 0; i < fta.actions.size(); ++i)
    uid_to_index.emplace(fta.actions[i].uid, i);

  PreparedFTA prepared{fta, {}};
  prepared.actions.resize(fta.actions.size());

  for (unsigned i = 0; i < fta.actions.size(); ++i) {
    data::Action const & a = fta.actions[i];
    PreparedAction & p = prepared.actions[i];

    p.successors.reserve(a.successors.size());
    for (auto const & succ : a.successors) {
      auto it = uid_to_index.find(succ);
      if (it == uid_to_index.end())
        throw autocog::ConfigError(
          "FTA action '" + a.uid + "' references unknown successor '" + succ + "'", a.uid);
      p.successors.push_back(it->second);
    }

    if (auto const * t = std::get_if<data::TextAction>(&a.body)) {
      if (!t->text.empty()) p.tokens = model.tokenize(t->text, false, true);
    } else if (auto const * c = std::get_if<data::CompleteAction>(&a.body)) {
      // Prime the per-model mask cache (generation and stop vocabs) so the
      // masks are ready at generation time.
      for (auto const & ref : {c->vocab, c->stop}) {
        if (!ref) continue;
        auto vit = fta.vocabs.find(*ref);
        if (vit == fta.vocabs.end())
          throw autocog::ConfigError(
            "FTA action '" + a.uid + "' references unknown vocab '" + *ref + "'", a.uid);
        model.vocab_mask(*ref, vit->second);
      }
    } else if (auto const * ch = std::get_if<data::ChooseAction>(&a.body)) {
      p.choices.reserve(ch->choices.size());
      for (auto const & s : ch->choices) p.choices.push_back(model.tokenize(s, false, true));
    }
  }
  prepared.prepare_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return prepared;
}

void detokenize(ModelID const id, data::FTT & ftt) {
  Model & model = Manager::get_model(id);
  std::function<void(data::FTTNode &)> walk = [&](data::FTTNode & node) {
    node.text = node.tokens.empty() ? std::string{}
                                    : model.detokenize(node.tokens, false, false);
    for (auto & child : node.children) walk(child);
  };
  walk(ftt.root);
}

}
