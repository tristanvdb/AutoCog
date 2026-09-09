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

  float proba() const { return logprobs.empty() ? 0.0f : std::exp(-logprob / logprobs.size()); }
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

// A candidate's greedy continuation being scored across rollout waves: the
// candidate's context (base + beam + emitted token), the accumulated -log P
// of the continuation so far, and which pruned-in beam it feeds back into.
struct RolloutState {
  size_t beam_index;      // into next_beams
  TokenSequence prefix;
  double sum{0.0};
  unsigned n{0};
  bool done{false};
};

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
  data::CompleteAction const & ca, std::vector<bool> const * stop_mask,
  std::vector<bool> const & mask, TokenSequence const & base_tokens,
  std::vector<BeamState> & current_beams, unsigned & num_token_eval,
  PerfCounters & perf
) {
  // Expand the whole beam frontier with one batched decode.
  std::vector<BeamState> next_beams;
  std::vector<BeamState const *> expanding;
  std::vector<TokenSequence> targets;
  for (BeamState const & beam : current_beams) {
    if (beam.stopped) { next_beams.push_back(beam); continue; }
    TokenSequence target = base_tokens;
    target.insert(target.end(), beam.tokens.begin(), beam.tokens.end());
    expanding.push_back(&beam);
    targets.push_back(std::move(target));
  }
  // `topk` = candidate tokens sampled per expanded beam; `beams` = hypotheses
  // surviving the step (prune_beams below). Unset topk keeps the historical
  // implicit topk == beams.
  size_t const topk = ca.topk ? *ca.topk : ca.beams;
  std::vector<FrontierResult> expansions;
  unsigned const decoded = model.topk_frontier(targets, mask, topk, expansions, ctx);
  num_token_eval += decoded;
  perf.complete.tokens_eval += static_cast<unsigned>(targets.size());
  perf.complete.tokens_restore += decoded - static_cast<unsigned>(targets.size());

  std::vector<RolloutState> rollouts;
  for (size_t b = 0; b < expanding.size(); ++b) {
    BeamState const & beam = *expanding[b];
    FrontierResult const & fr = expansions[b];
    for (size_t i = 0; i < fr.tokens.size(); ++i) {
      BeamState & new_beam = next_beams.emplace_back(beam);
      new_beam.tokens.push_back(fr.tokens[i]);
      new_beam.logprobs.push_back(fr.logprobs[i]);
      new_beam.logprob += fr.logprobs[i];

      if (ca.repetition) {
        TokenSequence beam_tokens;
        beam_tokens.insert(beam_tokens.end(), base_tokens.begin(), base_tokens.end());
        beam_tokens.insert(beam_tokens.end(), new_beam.tokens.begin(), new_beam.tokens.end());
        calculate_repetition_penalty(beam_tokens, new_beam.repetition_penalty, ca.repetition.value());
      }

      // A completion stops when it emits any token of the stop vocab; the stop
      // token itself is not part of the output. No stop vocab: never stops early.
      TokenID const emitted = new_beam.tokens.back();
      new_beam.stopped = stop_mask && (*stop_mask)[emitted];
      if (new_beam.stopped)
        new_beam.tokens.pop_back();

      // ahead > 1: this candidate's greedy continuation is scored below
      // (ahead includes the candidate itself, so ahead=1 costs nothing extra).
      if (ca.ahead > 1 && !new_beam.stopped) {
        RolloutState r;
        r.beam_index = next_beams.size() - 1;
        r.prefix = targets[b];
        r.prefix.push_back(emitted);
        rollouts.push_back(std::move(r));
      }
    }
  }

  // Lookahead rollouts advance in waves — one batched decode per depth, all
  // candidates of all beams at once. A continuation that samples a stop token
  // ends naturally and leaves its wave.
  for (unsigned step = 0; ca.ahead > 1 && step + 1 < ca.ahead; ++step) {
    std::vector<size_t> live;
    std::vector<TokenSequence> wave;
    for (size_t r = 0; r < rollouts.size(); ++r)
      if (!rollouts[r].done) { live.push_back(r); wave.push_back(rollouts[r].prefix); }
    if (wave.empty()) break;
    std::vector<FrontierResult> wres;
    unsigned const wdecoded = model.topk_frontier(wave, mask, 1, wres, ctx);
    num_token_eval += wdecoded;
    perf.complete.tokens_eval += static_cast<unsigned>(wave.size());
    perf.complete.tokens_restore += wdecoded - static_cast<unsigned>(wave.size());
    perf.lookahead_tokens += static_cast<unsigned>(wave.size());
    for (size_t k = 0; k < live.size(); ++k) {
      RolloutState & r = rollouts[live[k]];
      if (wres[k].tokens.empty()) { r.done = true; continue; }
      r.sum += wres[k].logprobs[0];
      r.n += 1;
      if (stop_mask && (*stop_mask)[wres[k].tokens[0]]) r.done = true;
      else r.prefix.push_back(wres[k].tokens[0]);
    }
  }
  for (RolloutState const & r : rollouts)
    if (r.n) next_beams[r.beam_index].lookahead_bonus = std::exp(static_cast<float>(-r.sum / r.n));

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

  auto [model, ctx] = this->restore(state, perf_.complete);
  std::vector<bool> const & gen_mask = ca.vocab
      ? model.vocab_mask(*ca.vocab, fta.vocabs.at(*ca.vocab))
      : model.full_vocab_mask();
  std::vector<bool> const * stop_mask = ca.stop
      ? &model.vocab_mask(*ca.stop, fta.vocabs.at(*ca.stop))
      : nullptr;

  // The stop set is unioned into the generation mask so a restrictive vocab
  // cannot make termination unreachable. A vocab that must fill its exact
  // token budget (e.g. exactly three digits) simply has no stop set.
  std::vector<bool> mask = gen_mask;
  if (stop_mask)
    for (size_t i = 0; i < mask.size() && i < stop_mask->size(); ++i)
      if ((*stop_mask)[i]) mask[i] = true;

  std::vector<BeamState> beams;
  beams.emplace_back();

  unsigned num_token_eval = 0;
  for (unsigned pos = 0; pos < ca.length; ++pos) {
    bool should_stop = beam_search_step(model, ctx, ca, stop_mask, mask, state.tokens, beams, num_token_eval, perf_);
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
