
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/fta.hxx"
#include "autocog/llama/ftt.hxx"

#include <llama.h>

#include <iostream>
#include <stdexcept>
#include <vector>
#include <queue>
#include <algorithm>
#include <cmath>

namespace autocog { namespace llama {

struct Beam {
  TokenSequence tokens;
  ProbaSequence probas;
  float cumulative_log_prob;
  bool stopped;
 
  float get_probability() const {
    return std::exp(cumulative_log_prob);
  }
};

// Expand a single beam by evaluating next token
static void expand_beam(
  Model& model,
  ContextID ctx,
  const Beam& beam,
  const Completion& action,
  const TokenSequence& base_tokens,
  std::vector<Beam>& next_beams,
  unsigned& num_token_eval
) {
  TokenSequence context_tokens = base_tokens;
  context_tokens.insert(context_tokens.end(), beam.tokens.begin(), beam.tokens.end());
  model.set_tokens(context_tokens, ctx);
 
  // Get top candidates
  std::vector<TokenID> topk_tokens;
  std::vector<float> topk_logits;
  size_t n_candidates = static_cast<size_t>(action.beams * action.ahead);
  num_token_eval += model.eval_topk_tokens(
    action.vocab.mask,
    n_candidates,
    topk_tokens,
    topk_logits,
    ctx
  );
 
  // Create new beams from candidates
  for (size_t i = 0; i < topk_tokens.size(); ++i) {
    TokenID token = topk_tokens[i];
    float logit = topk_logits[i];
   
    // Check if this is a stop token
    bool is_stop = std::find(action.stop.begin(), action.stop.end(), token) != action.stop.end();
   
    if (is_stop) {
      Beam new_beam = beam;
      new_beam.stopped = true;
      next_beams.push_back(new_beam);
    } else {
      Beam new_beam = beam;
      new_beam.tokens.push_back(token);
      new_beam.probas.push_back(std::exp(logit)); // Simplified probability
      new_beam.cumulative_log_prob += logit;
      next_beams.push_back(new_beam);
    }
  }
}

// Prune beams to keep only top k
static void prune_beams(
  std::vector<Beam>& beams,
  unsigned beam_width
) {
  // Sort by score
  std::sort(beams.begin(), beams.end(), [](const Beam& a, const Beam& b) {
    if (a.stopped != b.stopped) {
      return a.stopped > b.stopped;  // Prioritize stopped beams
    }
    return a.cumulative_log_prob > b.cumulative_log_prob;
  });
 
  // Keep top beams
  std::vector<Beam> pruned;
  size_t kept_active = 0;
  for (const auto& beam : beams) {
    if (beam.stopped) {
      pruned.push_back(beam);
    } else if (kept_active < beam_width) {
      pruned.push_back(beam);
      kept_active++;
    }
  }
 
  beams = std::move(pruned);
}

// Run beam search for one position
static bool beam_search_step(
  Model& model,
  ContextID ctx,
  const Completion& action,
  const TokenSequence& base_tokens,
  std::vector<Beam>& current_beams,
  unsigned& num_token_eval
) {
  std::vector<Beam> next_beams;
 
  for (const Beam& beam : current_beams) {
    if (beam.stopped) {
      next_beams.push_back(beam);
      continue;
    }
    expand_beam(model, ctx, beam, action, base_tokens, next_beams, num_token_eval);
  }

  // Check for early termination
  bool all_stopped = std::all_of(next_beams.begin(), next_beams.end(), [](const Beam& b) { return b.stopped; });
 
  if (all_stopped) {
    current_beams = std::move(next_beams);
    return true; // Signal early termination
  }
 
  // Prune and update beams
  prune_beams(next_beams, action.beams);
 
  if (next_beams.empty()) {
    throw std::runtime_error("No valid beams remaining in completion");
  }
 
  current_beams = std::move(next_beams);
  return false; // Continue beam search
}

// Add selected beams to FTT
void add_beams_to_ftt(
  std::vector<Beam> const & beams,
  Completion const & action,
  PathState & state,
  Evaluation * eval
) {
  // Sort by score
  std::vector<Beam> sorted_beams = beams;
  std::sort(sorted_beams.begin(), sorted_beams.end(), [](const Beam& a, const Beam& b) {
    return a.cumulative_log_prob > b.cumulative_log_prob;
  });

  // Add top beams to FTT
  size_t n_keep = std::min(static_cast<size_t>(action.ahead), sorted_beams.size());

  for (size_t i = 0; i < n_keep; ++i) {
    const Beam& beam = sorted_beams[i];
    float probability = beam.get_probability();

    FTT & child = state.parent.add(action.id, beam.tokens, beam.probas, probability);

    // Apply threshold pruning
    if (probability < action.threshold) {
      child.pruned = true;
      continue;
    }

    // Enqueue next action
    if (!action.successors.empty()) {
      TokenSequence next_tokens = state.tokens;
      next_tokens.insert(next_tokens.end(), beam.tokens.begin(), beam.tokens.end());
      eval->enqueue(action.successors[0], child, next_tokens, state);
    }
  }
}

unsigned Evaluation::evaluate_completion(PathState & state) {
  auto [model, ctx] = this->restore_context(state);
  Completion const & action = this->fta.action(state.action).as<Completion>();
  std::cerr << "Executing Completion #" << action.id << std::endl;
  std::cerr << " - beams: " << action.beams << std::endl;
  std::cerr << " - length: " << action.length << std::endl;
  std::cerr << " - ahead: " << action.ahead << std::endl;
  std::cerr << " - threshold: " << action.threshold << std::endl;
 
  unsigned num_token_eval = 0;
 
  // Initialize beam search
  std::vector<Beam> current_beams;
  current_beams.push_back({TokenSequence(), ProbaSequence(), 0.0f, false});
 
  // Run beam search
  for (unsigned pos = 0; pos < action.length; ++pos) {
    std::cerr << " - pos[" << pos << "]..." << std::endl;
    bool should_stop = beam_search_step(
      model, ctx, action, state.tokens, current_beams, num_token_eval
    );
   
    if (should_stop) {
      break;
    }
  }
 
  add_beams_to_ftt(current_beams, action, state, this);
 
  return num_token_eval;
}

} }

