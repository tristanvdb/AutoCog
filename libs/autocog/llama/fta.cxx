
#include "autocog/llama/fta.hxx"

#include <map>
#include <string>

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

// TODO review

FTA::FTA(pybind11::dict const & pydata) {
    // Get the actions dictionary from Python FTA
    if (!pydata.contains("actions")) {
        throw std::runtime_error("FTA dictionary missing 'actions' field");
    }
    
    auto py_actions = pydata["actions"].cast<pybind11::dict>();
    
    // First pass: create all actions and build ID mapping
    std::map<std::string, NodeID> uid_to_id;
    
    for (auto item : py_actions) {
        std::string uid = item.first.cast<std::string>();
        auto action_dict = item.second.cast<pybind11::dict>();
        
        if (!action_dict.contains("__type__")) {
            throw std::runtime_error("Action missing '__type__' field: " + uid);
        }
        
        std::string action_type = action_dict["__type__"].cast<std::string>();
        float threshold = action_dict.contains("threshold") ? action_dict["threshold"].cast<float>() : 0.0f;
        
        NodeID node_id = actions.size();  // Assign sequential IDs
        uid_to_id[uid] = node_id;
        
        std::unique_ptr<Action> action;
        
        if (action_type == "Text") {
            action = std::make_unique<Text>(node_id, threshold);
            Text* text_action = static_cast<Text*>(action.get());
            
            // Set tokens
            if (action_dict.contains("tokens")) {
                auto py_tokens = action_dict["tokens"].cast<pybind11::list>();
                text_action->tokens.clear();
                for (auto token : py_tokens) {
                    text_action->tokens.push_back(token.cast<TokenID>());
                }
            }
            
        } else if (action_type == "Completion") {
            // Extract parameters with defaults
            unsigned length = action_dict.contains("length") ? action_dict["length"].cast<unsigned>() : 1;
            unsigned beams = action_dict.contains("beams") ? action_dict["beams"].cast<unsigned>() : 1;
            unsigned ahead = action_dict.contains("ahead") ? action_dict["ahead"].cast<unsigned>() : 1;
            
            action = std::make_unique<Completion>(node_id, threshold, length, beams, ahead);
            Completion* completion_action = static_cast<Completion*>(action.get());
            
            // Set stop tokens
            if (action_dict.contains("stop_tokens")) {
                auto py_stop = action_dict["stop_tokens"].cast<pybind11::list>();
                completion_action->stop.clear();
                for (auto token : py_stop) {
                    completion_action->stop.push_back(token.cast<TokenID>());
                }
            }
            
            // Set vocabulary mask
            if (action_dict.contains("vocab_mask")) {
                auto py_mask = action_dict["vocab_mask"].cast<pybind11::list>();
                completion_action->vocab.mask.clear();
                completion_action->vocab.mask.reserve(py_mask.size());
                for (auto val : py_mask) {
                    completion_action->vocab.mask.push_back(val.cast<bool>());
                }
            }
            
        } else if (action_type == "Choice") {
            unsigned width = action_dict.contains("width") ? action_dict["width"].cast<unsigned>() : 1;
            action = std::make_unique<Choice>(node_id, threshold, width);
            Choice* choice_action = static_cast<Choice*>(action.get());
            
            // Set choices
            if (action_dict.contains("choices")) {
                auto py_choices = action_dict["choices"].cast<pybind11::list>();
                choice_action->choices.clear();
                choice_action->choices.reserve(py_choices.size());
                
                for (auto py_choice : py_choices) {
                    auto py_tokens = py_choice.cast<pybind11::list>();
                    TokenSequence choice_tokens;
                    choice_tokens.reserve(py_tokens.size());
                    for (auto token : py_tokens) {
                        choice_tokens.push_back(token.cast<TokenID>());
                    }
                    choice_action->choices.push_back(std::move(choice_tokens));
                }
            }
            
        } else {
            throw std::runtime_error("Unknown action type: " + action_type);
        }
        
        actions.push_back(std::move(action));
    }
    
    // Second pass: set up successors using the UID mapping
    for (auto item : py_actions) {
        std::string uid = item.first.cast<std::string>();
        auto action_dict = item.second.cast<pybind11::dict>();
        
        NodeID node_id = uid_to_id[uid];
        Action* action = actions[node_id].get();
        
        // Add successors
        if (action_dict.contains("successors")) {
            auto py_successors = action_dict["successors"].cast<pybind11::list>();
            for (auto successor : py_successors) {
                std::string successor_uid = successor.cast<std::string>();
                if (uid_to_id.find(successor_uid) == uid_to_id.end()) {
                    throw std::runtime_error("Unknown successor UID: " + successor_uid);
                }
                action->successors.push_back(uid_to_id[successor_uid]);
            }
        }
    }
}

} }

