
#include "autocog/llama/xfta/fta.hxx"
#include "autocog/llama/xfta/model.hxx"

#include <map>
#include <string>

namespace autocog::llama::xfta {

Action::Action(
  ActionKind const kind_,
  ActionID const id_,
  std::string const & name_
) :
  kind(kind_),
  id(id_),
  name(name_),
  successors()
{}

Text::Text(
  ActionID const id_,
  std::string const & name_,
  bool const eval
) :
  Action(ActionKind::Text, id_, name_),
  evaluate(eval)
{}

Completion::Completion(
  ActionID const id_,
  std::string const & name_,
  float threshold_,
  unsigned length_,
  unsigned beams_,
  unsigned ahead_,
  unsigned width_,
  std::optional<float> repetition_,
  std::optional<float> diversity_
) :
  Action(ActionKind::Completion, id_, name_),
  threshold(threshold_),
  length(length_),
  beams(beams_),
  ahead(ahead_),
  width(width_),
  repetition(repetition_),
  diversity(diversity_)
{}

Choice::Choice(
  ActionID const id_,
  std::string const & name_,
  float threshold_,
  unsigned width_
) :
  Action(ActionKind::Choice, id_, name_),
  threshold(threshold_),
  width(width_)
{}

Action const & FTA::action(ActionID const id) const {
  Action const & action = *(this->actions.at(id));
  if (action.id != id) {
    throw std::runtime_error("Action's ID does not match position in FTA::actions!");
  }
  return action;
}

}

