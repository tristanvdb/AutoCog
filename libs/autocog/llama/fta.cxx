
#include "autocog/llama/fta.hxx"
#include "autocog/llama/model.hxx"

#include <map>
#include <string>

namespace autocog { namespace llama {

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

// TODO review

FTA::FTA(Model const & model, pybind11::dict const & pydata) {
    // Get the actions dictionary from Python FTA
    if (!pydata.contains("actions")) {
        throw std::runtime_error("FTA dictionary missing 'actions' field");
    }
    
    auto py_actions = pydata["actions"].cast<pybind11::list>();
    
    // First pass: create all actions and build ID mapping
    std::map<std::string, ActionID> uid_to_id;

    for (auto item : py_actions) {
        auto action_dict = item.cast<pybind11::dict>();

        if (!action_dict.contains("uid")) {
          throw std::runtime_error("Action missing 'uid' field.");
        }
        auto uid = action_dict["uid"].cast<std::string>();
        if (!action_dict.contains("__type__")) {
          throw std::runtime_error("Action missing '__type__' field: " + uid);
        }
        
        std::string action_type = action_dict["__type__"].cast<std::string>();
        
        ActionID node_id = actions.size();  // Assign sequential IDs
        uid_to_id[uid] = node_id;
        
        std::unique_ptr<Action> action;
        
        if (action_type == "Text") {
          bool evaluate = false;
          if (action_dict.contains("evaluate")) evaluate = action_dict["evaluate"].cast<bool>();

          action = std::make_unique<Text>(node_id, uid, evaluate);
          Text * text_action = static_cast<Text*>(action.get());

          auto py_tokens = action_dict["tokens"].cast<pybind11::list>();
          text_action->tokens.clear();
          for (auto token : py_tokens) {
            text_action->tokens.push_back(token.cast<TokenID>());
          }
            
        } else if (action_type == "Complete") {
            float threshold = action_dict["threshold"].cast<float>();
            unsigned length = action_dict["length"].cast<unsigned>();
            unsigned beams = action_dict["beams"].cast<unsigned>();
            unsigned ahead = action_dict["ahead"].cast<unsigned>();
            unsigned width = action_dict["width"].cast<unsigned>();
            std::optional<float> repetition = std::nullopt;
            if (action_dict.contains("repetition") && !action_dict["repetition"].is_none()) repetition = action_dict["repetition"].cast<float>();
            std::optional<float> diversity = std::nullopt;
            if (action_dict.contains("diversity") && !action_dict["diversity"].is_none()) repetition = action_dict["diversity"].cast<float>();

            action = std::make_unique<Completion>(node_id, uid, threshold, length, beams, ahead, width, repetition, diversity);
            Completion* completion_action = static_cast<Completion*>(action.get());
            
            // Set stop tokens
            if (!action_dict.contains("stop")) {
              throw std::runtime_error("Completion missing 'stop' field: " + uid);
            } else {
              auto py_stop = action_dict["stop"].cast<pybind11::list>();
              completion_action->stop.clear();
              for (auto token : py_stop) {
                completion_action->stop.push_back(token.cast<TokenID>());
              }
            }
            
            // Set vocabulary mask
            completion_action->vocab.mask.clear();
            completion_action->vocab.mask.reserve(model.vocab_size());
            for (auto tok_msk : action_dict["vocab"]) {
              completion_action->vocab.mask.push_back(tok_msk.cast<bool>());
            }

        } else if (action_type == "Choose") {
            float threshold = action_dict["threshold"].cast<float>();
            unsigned width = action_dict["width"].cast<unsigned>();
            action = std::make_unique<Choice>(node_id, uid, threshold, width);
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
        auto action_dict = item.cast<pybind11::dict>();
        auto uid = action_dict["uid"].cast<std::string>();
        
        ActionID node_id = uid_to_id[uid];
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

