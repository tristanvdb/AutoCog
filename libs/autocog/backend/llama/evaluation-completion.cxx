#include "autocog/backend/llama/evaluation.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/logging.hxx"
#include "autocog/utilities/exception.hxx"

#include <algorithm>
#include <cmath>
#include <set>
#include <variant>
#include <vector>

namespace autocog::backend::llama {

struct BeamState {
  TokenSequence tokens;
  ProbaSequence logprobs;
  float logprob{0.};
  float repetition_penalty = 1.0f;
  float diversity_bonus = 0.0f;
  float lookahead_bonus = 0.0f;
  bool stopped{false};

  float proba() const { return std::exp(-logprob / logprobs.size()); }
  float score() const { return (this->proba() + lookahead_bonus + diversity_bonus) / repetition_penalty; }
};

static float calculate_repetition_penalty(
  TokenSequence const & tokens, float & penalty, float const penalty_weight,
  size_t const min_length = 3, size_t const max_window_size = 256,
  float const length_weight = 1.0, float const recency_weight = 1.0
) {
  size_t window_size = std::min(tokens.size(), max_window_size);
  for (size_t i = min_length; i < tokens.size(); ++i) {
    size_t best_length = 0, best_distance = 0;
    size_t search_start = (i >= window_size) ? i - window_size : 0;
    for (size_t j = search_start; j < i; ++j) {
      size_t match_length = 0;
      while (j + match_length < i && i + match_length < tokens.size() &&
             tokens[j + match_length] == tokens[i + match_length]) {
        match_length++;
      }
      if (match_length >= min_length && match_length > best_length) {
        best_length = match_length;
        best_distance = i - j;
      }
    }
    if (best_length >= min_length) {
      float length_factor  = std::log(1. + length_weight  * best_length);
      float recency_factor = std::log(1. + recency_weight * best_distance);
      penalty *= (1.0f + penalty_weight * length_factor / recency_factor);
    }
  }
  return penalty;
}

static float token_sequence_diversity(TokenSequence const & a, TokenSequence const & b) {
  std::set<TokenID> set_a(a.begin(), a.end());
  std::set<TokenID> set_b(b.begin(), b.end());
  std::set<TokenID> intersection;
  std::set_intersection(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
                        std::inserter(intersection, intersection.begin()));
  std::set<TokenID> union_set;
  std::set_union(set_a.begin(), set_a.end(), set_b.begin(), set_b.end(),
                 std::inserter(union_set, union_set.begin()));
  return 1.0f - (float)intersection.size() / union_set.size();
}

static void calculate_diversity_bonuses(std::vector<BeamState> & beams, float const weight) {
  for (size_t i = 0; i < beams.size(); ++i) {
    float diversity = 0.0f;
    for (size_t j = 0; j < beams.size(); ++j)
      if (i != j) diversity += token_sequence_diversity(beams[i].tokens, beams[j].tokens);
    beams[i].diversity_bonus = weight * diversity / (beams.size() - 1);
  }
}

static unsigned expand_beam(
  Model & model, ContextID ctx, BeamState const & beam,
  data::CompleteAction const & ca, TokenSequence const & stop,
  std::vector<bool> const & mask, TokenSequence const & base_tokens,
  std::vector<BeamState> & beams
) {
  TokenSequence context_tokens = base_tokens;
  context_tokens.insert(context_tokens.end(), beam.tokens.begin(), beam.tokens.end());
  model.set_tokens(context_tokens, ctx);

  std::vector<TokenID> topk_tokens;
  std::vector<float> topk_logits;
  unsigned num_token_eval = model.eval_topk_tokens(mask, ca.beams, topk_tokens, topk_logits, ctx);

  for (size_t i = 0; i < topk_tokens.size(); ++i) {
    BeamState & new_beam = beams.emplace_back(beam);
    new_beam.tokens.push_back(topk_tokens[i]);
    new_beam.logprobs.push_back(topk_logits[i]);
    new_beam.logprob += topk_logits[i];

    if (ca.repetition) {
      TokenSequence beam_tokens;
      beam_tokens.insert(beam_tokens.end(), base_tokens.begin(), base_tokens.end());
      beam_tokens.insert(beam_tokens.end(), new_beam.tokens.begin(), new_beam.tokens.end());
      calculate_repetition_penalty(beam_tokens, new_beam.repetition_penalty, ca.repetition.value());
    }

    new_beam.stopped = (stop.size() <= new_beam.tokens.size()) &&
      std::equal(stop.begin(), stop.end(), new_beam.tokens.end() - stop.size());
    if (new_beam.stopped)
      new_beam.tokens.erase(new_beam.tokens.end() - stop.size(), new_beam.tokens.end());
  }
  return num_token_eval;
}

static void prune_beams(std::vector<BeamState> & beams, unsigned beam_width) {
  std::sort(beams.begin(), beams.end(), [](BeamState const & a, BeamState const & b) {
    if (a.stopped != b.stopped) return a.stopped;
    return a.score() > b.score();
  });
  std::vector<BeamState> pruned;
  size_t kept_active = 0;
  for (BeamState const & beam : beams) {
    if (beam.stopped) pruned.push_back(beam);
    else if (kept_active < beam_width) { pruned.push_back(beam); kept_active++; }
  }
  beams = std::move(pruned);
}

static bool beam_search_step(
  Model & model, ContextID ctx,
  data::CompleteAction const & ca, TokenSequence const & stop,
  std::vector<bool> const & mask, TokenSequence const & base_tokens,
  std::vector<BeamState> & current_beams, unsigned & num_token_eval
) {
  std::vector<BeamState> next_beams;
  for (BeamState const & beam : current_beams) {
    if (beam.stopped) next_beams.push_back(beam);
    else num_token_eval += expand_beam(model, ctx, beam, ca, stop, mask, base_tokens, next_beams);
  }
  if (ca.diversity) calculate_diversity_bonuses(next_beams, ca.diversity.value());

  bool all_stopped = std::all_of(next_beams.begin(), next_beams.end(),
                                 [](BeamState const & b) { return b.stopped; });
  if (all_stopped) { current_beams = std::move(next_beams); return true; }

  prune_beams(next_beams, ca.beams);
  if (next_beams.empty())
    throw autocog::utilities::InternalError("No valid beams remaining in completion");
  current_beams = std::move(next_beams);
  return false;
}

unsigned Evaluation::evaluate_completion(PathState & state) {
  data::FTA const & fta = prepared.fta;
  data::CompleteAction const & ca = std::get<data::CompleteAction>(fta.actions[state.action].body);
  PreparedAction const & p = prepared.actions[state.action];

  auto [model, ctx] = this->restore(state);
  std::vector<bool> const & mask = ca.vocab
      ? model.vocab_mask(*ca.vocab, fta.vocabs.at(*ca.vocab))
      : model.full_vocab_mask();

  std::vector<BeamState> beams;
  beams.emplace_back();

  unsigned num_token_eval = 0;
  for (unsigned pos = 0; pos < ca.length; ++pos) {
    bool should_stop = beam_search_step(model, ctx, ca, p.stop, mask, state.tokens, beams, num_token_eval);
    if (should_stop) break;
  }

  std::sort(beams.begin(), beams.end(),
            [](BeamState const & a, BeamState const & b) { return a.score() > b.score(); });

  unsigned count = 0;
  for (auto & beam : beams) {
    data::FTTNode & child = grow(state.parent, state.action, fta, beam.tokens, beam.logprobs);
    child.pruned = (count >= ca.width) || (count > 0 && beam.proba() < ca.threshold);
    if (!child.pruned) this->enqueue(p.successors[0], child, state);
    count++;
  }
  return num_token_eval;
}

}
