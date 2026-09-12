#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/errors.hxx"
#include "autocog/utilities/exception.hxx"

#include <algorithm>
#include <cmath>
#include <variant>
#include <vector>

namespace autocog::backend::llama {

struct ChoiceResult {
  size_t index;
  ProbaSequence logprobs;
  float rank;       ///< score under the ranking metric (orders candidates)
  float threshold;  ///< score under the threshold metric (pruning compare)
  ChoiceResult(size_t index_, ProbaSequence logprobs_, float rank_, float threshold_)
    : index(index_), logprobs(logprobs_), rank(rank_), threshold(threshold_) {}
};

/// Candidate score for one metric, from the summed NLL of the candidate's
/// tokens: "mean" = per-token geometric-mean probability, "sum" = joint
/// probability, "bytes" = per-byte normalization over the choice text.
static float choice_score(std::string const & metric, float total_nll,
                          size_t tokens, size_t bytes) {
  if (metric == "sum")   return std::exp(-total_nll);
  if (metric == "bytes") return bytes ? std::exp(-total_nll / static_cast<float>(bytes)) : 0.0f;
  if (metric == "mean")  return tokens ? std::exp(-total_nll / static_cast<float>(tokens)) : 0.0f;
  throw autocog::SchemaError("Unknown choice scoring metric '" + metric + "'", metric);
}

unsigned Evaluation::evaluate_choice(PathState & state) {
  data::ChooseAction const & ca = std::get<data::ChooseAction>(prepared.fta.actions[state.action].body);
  PreparedAction const & p = prepared.actions[state.action];

  if (p.choices.empty())
    throw autocog::utilities::InternalError("Choice action has no choices");
  if (p.successors.size() != p.choices.size())
    throw autocog::utilities::InternalError("Choice action must have as many successors as choices");

  unsigned num_token_eval = 0;
  std::vector<ChoiceResult> results;

  for (size_t idx = 0; idx < p.choices.size(); ++idx) {
    auto [model, ctx] = this->restore(state, perf_.choose);
    ProbaSequence logprobs;
    num_token_eval += model.eval_sequences(p.choices[idx], logprobs, ctx);
    perf_.choose.tokens_eval += static_cast<unsigned>(p.choices[idx].size());

    float total = 0.;
    for (float lpb : logprobs) total += lpb;
    size_t const bytes = idx < ca.choices.size() ? ca.choices[idx].size() : 0;
    float const rank = choice_score(ca.ranking, total, logprobs.size(), bytes);
    float const thr  = ca.threshold_metric == ca.ranking
                     ? rank
                     : choice_score(ca.threshold_metric, total, logprobs.size(), bytes);

    results.emplace_back(idx, logprobs, rank, thr);
    state.context.reset(); // TODO remove once context saving/restore/rewind is implemented
  }

  std::sort(results.begin(), results.end(),
    [](const ChoiceResult& a, const ChoiceResult& b) { return a.rank > b.rank; });

  unsigned count = 0;
  for (const auto & result : results) {
    auto & choice_tokens = p.choices[result.index];
    data::FTTNode & child = grow(state.parent, state.action, prepared.fta, choice_tokens, result.logprobs);
    if (count > 0 && result.threshold < ca.threshold) child.pruned = data::Pruned::Threshold;
    else if (count >= ca.width)                       child.pruned = data::Pruned::Width;
    if (child.pruned == data::Pruned::No) {
      this->enqueue(p.successors[result.index], child, state);
    }
    count++;
  }
  return num_token_eval;
}

}
