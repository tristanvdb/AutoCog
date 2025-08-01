
#include "autocog/llama/fta.hxx"

namespace autocog { namespace llama {

Action::Action(
  ActionKind const kind_, NodeID const id_, float threshold_
) : kind(kind_), id(id_), threshold(threshold_), successors() {}

Text::Text(NodeID const id_, float threshold_) : Action(ActionKind::Text, id_, threshold_) {}

Completion::Completion(
  NodeID const id_, float threshold_, unsigned length_, unsigned beams_, unsigned ahead_
) : Action(ActionKind::Completion, id_, threshold_), length(length_), beams(beams_), ahead(ahead_) {}

Choice::Choice(NodeID const id_, float threshold_, unsigned width_) : Action(ActionKind::Choice, id_, threshold_), width(width_) {}

Action const & FTA::action(NodeID const & id) const {
  Action const & action = *(this->actions.at(id));
  if (action.id != id) {
    throw std::runtime_error("Action's ID does not match position in FTA::actions!");
  }
  return action;
}

Text & FTA::insert(float threshold_) {
  NodeID id = this->actions.size();
  Text * action = new Text(id, threshold_);
  this->actions.push_back(std::unique_ptr<Action>(action));
  return *action;
}

Completion & FTA::insert(float threshold_, unsigned length_, unsigned beams_, unsigned ahead_) {
  NodeID id = this->actions.size();
  Completion * action = new Completion(id, threshold_, length_, beams_, ahead_);
  this->actions.push_back(std::unique_ptr<Action>(action));
  return *action;
}

Choice & FTA::insert(float threshold_, unsigned width_) {
  NodeID id = this->actions.size();
  Choice * action = new Choice(id, threshold_, width_);
  this->actions.push_back(std::unique_ptr<Action>(action));
  return *action;
}

FTA::FTA(pybind11::dict const & pydata) {
  throw std::runtime_error("Loading FTA from python dictionary is not implemented yet!");
}

} }

