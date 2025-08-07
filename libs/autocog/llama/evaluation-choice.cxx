
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/fta.hxx"
#include "autocog/llama/ftt.hxx"

#include <llama.h>

#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace autocog { namespace llama {

unsigned Evaluation::evaluate_choice(PathState & state) {
  auto [model, ctx] = this->restore_context(state);
  Choice const & action = this->fta.action(state.action).as<Choice>();
  std::cerr << "Executing Choice     #" << action.id << std::endl;
  std::cerr << " - width: " << action.width << std::endl;
  std::cerr << " - number of choices: " << action.choices.size() << std::endl;
  
  if (action.choices.empty()) {
    throw std::runtime_error("Choice action has no choices");
  }

  if (action.successors.size() != action.choices.size()) {
    throw std::runtime_error("Choice action must have as many successors as choices");
  }
  
  unsigned num_token_eval = 0;
  
  // Structure to hold choice evaluation results
  struct ChoiceResult {
    size_t index;
    TokenSequence tokens;
    ProbaSequence probas;
    float total_probability;
  };
  std::vector<ChoiceResult> results;
  
  // Evaluate ALL choices in full
  for (size_t i = 0; i < action.choices.size(); ++i) {
    TokenSequence const & choice_tokens = action.choices[i];
    std::cerr << " - choice[" << i << "]:" << std::endl;
    std::cerr << "   - number of tokens: " << choice_tokens.size() << std::endl;
    
    // Save current state to restore after evaluation
    TokenSequence saved_tokens = model.get_tokens_const(ctx);
    
    // Evaluate this choice
    ProbaSequence probas;
    num_token_eval += model.eval_sequences(choice_tokens, probas, ctx);
    
    // Calculate total probability for this choice
    // TODO: In the future, support different probability strategies:
    // - product (current): p_total = p1 * p2 * ... * pn
    // - average: p_total = (p1 + p2 + ... + pn) / n
    // - min: p_total = min(p1, p2, ..., pn)
    // - weighted: custom weighting function
    // This could be specified as a parameter in the Choice action
    float total_prob = 1.0f;
    for (float p : probas) {
      total_prob *= p;  // Product strategy (default)
    }
    std::cerr << "   - probability: " << total_prob << std::endl;
    
    results.push_back({i, choice_tokens, probas, total_prob});
    
    // Restore context state for next choice evaluation
    model.set_tokens(saved_tokens, ctx);
  }
  
  // Sort by probability (descending)
  std::sort(results.begin(), results.end(), 
    [](const ChoiceResult& a, const ChoiceResult& b) {
      return a.total_probability > b.total_probability;
    });
  
  // Select top 'width' choices that are above threshold
  std::vector<size_t> selected_indices;
  
  for (const auto& result : results) {
    if (result.total_probability >= action.threshold) {
      selected_indices.push_back(result.index);
      if (selected_indices.size() >= action.width) {
        break;
      }
    }
  }
  
  // If none above threshold, pick the best one
  if (selected_indices.empty() && !results.empty()) {
    selected_indices.push_back(results[0].index);
  }
  
  // Add selected choices to FTT and enqueue their corresponding successors
  for (size_t idx : selected_indices) {
    // Find the result for this index
    auto it = std::find_if(results.begin(), results.end(),
      [idx](const ChoiceResult& r) { return r.index == idx; });
    
    if (it != results.end()) {
      // Add this choice to the FTT
      FTT& child = state.parent.add(action.id, it->tokens, it->probas, it->total_probability);
      
      // Mark as pruned if below threshold (for tracking purposes)
      if (it->total_probability < action.threshold) {
        child.pruned = true;
        // Note: We still enqueue it since it was the best available
      }
      
      // Enqueue the corresponding successor action for this specific choice
      ActionID next_action = action.successors[idx];
      
      // Prepare tokens for next action (current context + chosen tokens)
      TokenSequence next_tokens = model.get_tokens_const(ctx);
      next_tokens.insert(next_tokens.end(), it->tokens.begin(), it->tokens.end());
      
      this->enqueue(next_action, child, next_tokens, state);
    }
  }
  
  return num_token_eval;
}

} }

