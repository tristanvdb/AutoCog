
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/fta.hxx"
#include "autocog/llama/ftt.hxx"

#include <llama.h>

#include <stdexcept>
#include <vector>
#include <queue>
#include <algorithm>
#include <cmath>

#if VERBOSE
#  include <iostream>
#endif

#define DEBUG_Evaluation_evaluate_completion VERBOSE && 1

namespace autocog { namespace llama {

struct BeamState {
  TokenSequence tokens;
  ProbaSequence logprobs;
  float logprob{0.};
  bool stopped{false};

  float proba() const {
    return std::exp(-logprob/logprobs.size());
  }
};

// Expand a single beam by evaluating next token
static unsigned expand_beam(
  Model & model,
  ContextID ctx,
  BeamState const & beam,
  Completion const & action,
  TokenSequence const & base_tokens,
  std::vector<BeamState> & beams
) {
  TokenSequence context_tokens = base_tokens;
  context_tokens.insert(context_tokens.end(), beam.tokens.begin(), beam.tokens.end());
  model.set_tokens(context_tokens, ctx);
 
  std::vector<TokenID> topk_tokens;
  std::vector<float> topk_logits;
  unsigned num_token_eval = model.eval_topk_tokens(
    action.vocab.mask,
    action.beams,
    topk_tokens,
    topk_logits,
    ctx
  );

  for (size_t i = 0; i < topk_tokens.size(); ++i) {
    BeamState & new_beam = beams.emplace_back(beam);
    new_beam.tokens.push_back(topk_tokens[i]);
    new_beam.logprobs.push_back(topk_logits[i]);
    new_beam.logprob += topk_logits[i];
    new_beam.stopped = (
      action.stop.size() <= new_beam.tokens.size()
    ) && std::equal(
      action.stop.begin(), 
      action.stop.end(),
      new_beam.tokens.end() - action.stop.size()
    );
    if (new_beam.stopped) {
      new_beam.tokens.erase(new_beam.tokens.end() - action.stop.size(), new_beam.tokens.end());
    }
  }
  return num_token_eval;
}

// Prune beams to keep only top k
static void prune_beams(
  std::vector<BeamState> & beams,
  unsigned beam_width
) {
  // Sort by score
  std::sort(beams.begin(), beams.end(), [](BeamState const & a, BeamState const & b) {
    if (a.stopped != b.stopped) return a.stopped;
    return a.proba() < b.proba();
  });
 
  // Keep top beams
  std::vector<BeamState> pruned;
  size_t kept_active = 0;
  for (BeamState const & beam : beams) {
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
  Model & model,
  ContextID ctx,
  const Completion& action,
  const TokenSequence& base_tokens,
  std::vector<BeamState> & current_beams,
  unsigned & num_token_eval
) {
  std::vector<BeamState> next_beams;
 
  for (BeamState const & beam : current_beams) {
    if (beam.stopped) {
      next_beams.push_back(beam);
    } else {
      num_token_eval += expand_beam(model, ctx, beam, action, base_tokens, next_beams);
    }
  }

  // Check for early termination
  bool all_stopped = std::all_of(next_beams.begin(), next_beams.end(), [](BeamState const & b) { return b.stopped; });
 
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

unsigned Evaluation::evaluate_completion(PathState & state) {
#if DEBUG_Evaluation_evaluate_completion
  std::cerr << "Executing Completion #" << state.action << std::endl;
#endif
  Completion const & action = this->fta.action(state.action).as<Completion>();
#if DEBUG_Evaluation_evaluate_completion
  std::cerr << " - name: " << action.name << std::endl;
  std::cerr << " - beams: " << action.beams << std::endl;
  std::cerr << " - length: " << action.length << std::endl;
  std::cerr << " - ahead: " << action.ahead << std::endl;
  std::cerr << " - width: " << action.width << std::endl;
  std::cerr << " - threshold: " << action.threshold << std::endl;
#endif
  auto [model, ctx] = this->restore(state);
#if DEBUG_Evaluation_evaluate_completion
  std::cerr << " - Model   #" << model.id << std::endl;
  std::cerr << " - Context #" << ctx << std::endl;
#endif

  std::vector<BeamState> beams;
  beams.emplace_back();
 
  unsigned num_token_eval = 0; 
  for (unsigned pos = 0; pos < action.length; ++pos) {
#if DEBUG_Evaluation_evaluate_completion
    std::cerr << " - pos[" << pos << "]..." << std::endl;
#endif
    bool should_stop = beam_search_step(
      model, ctx, action, state.tokens, beams, num_token_eval
    );
   
    if (should_stop) {
      break;
    }
  }

  std::sort(beams.begin(), beams.end(), [](BeamState const & a, BeamState const & b) {
    return a.proba() > b.proba();
  });

  unsigned count = 0;
  for (auto & beam: beams) {
#if DEBUG_Evaluation_evaluate_completion
    std::cerr << " - beam[" << count << "]:" << std::endl;
    std::cerr << "   length: " << beam.tokens.size() << std::endl;
    std::cerr << "   logprob: " << beam.logprob << std::endl;
    std::cerr << "   proba: " << beam.proba() << std::endl;
#endif
    FTT & child = state.parent.add(action.id, beam.tokens, beam.logprobs);
    child.pruned = ( count > action.width ) || (count > 0 && beam.proba() < action.threshold);
    if (!child.pruned) {
      this->enqueue(action.successors[0], child, state);
    }
    count++;
  }

#if DEBUG_Evaluation_evaluate_completion
  std::cerr << " > evaluated: " << num_token_eval << std::endl;
#endif
 
  return num_token_eval;
}

} }

