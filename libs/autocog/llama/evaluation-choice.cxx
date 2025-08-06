
#include "autocog/llama/evaluation.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/fta.hxx"
#include "autocog/llama/ftt.hxx"

#include <stdexcept>

namespace autocog { namespace llama {

unsigned Evaluation::evaluate_choice(PathState & state) {
  auto [model,ctx] = this->restore_context(state);
  Choice const & action = this->fta.action(state.action).as<Choice>();
  throw std::runtime_error("Not implemented yet");
  return 0;
}

} }

