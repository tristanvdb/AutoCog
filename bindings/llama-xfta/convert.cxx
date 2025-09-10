
#include "convert.hxx"

#include "autocog/llama/xfta/manager.hxx"

#include <map>

namespace autocog::llama::xfta {

FTA convert_pydict_to_fta(ModelID const id, pybind11::dict const & pydata) {
  Model & model = Manager::get_model(id);
  FTA fta;
  
  // Get the actions dictionary from Python FTA
  if (!pydata.contains("actions")) {
      throw std::runtime_error("FTA dictionary missing 'actions' field");
  }
    
  auto py_actions = pydata["actions"].cast<pybind11::list>();
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
        
    ActionID node_id = fta.actions.size(); // Assign sequential IDs
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
            
      if (!action_dict.contains("stop")) {
        throw std::runtime_error("Completion missing 'stop' field: " + uid);
      } else {
        auto py_stop = action_dict["stop"].cast<pybind11::list>();
        completion_action->stop.clear();
        for (auto token : py_stop) {
          completion_action->stop.push_back(token.cast<TokenID>());
        }
      }
            
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
        
    fta.actions.push_back(std::move(action));
  }
    
  for (auto item : py_actions) {
    auto action_dict = item.cast<pybind11::dict>();
    auto uid = action_dict["uid"].cast<std::string>();

    ActionID node_id = uid_to_id[uid];
    Action * action = fta.actions[node_id].get();

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

  return fta;
}

pybind11::dict convert_ftt_to_pydict(ModelID const id, FTT const & ftt) {
  [[maybe_unused]] Model & model = Manager::get_model(id);

  pybind11::dict result;
  result["action"] = ftt.action;
    
  // Convert tokens
  pybind11::list token_list;
  for (TokenID token : ftt.tokens) {
    token_list.append(token);
  }
  result["tokens"] = token_list;

  // Convert probabilities  
  pybind11::list logprobs_list;
  for (float lpb : ftt.logprobs) {
    logprobs_list.append(lpb);
  }
  result["logprobs"] = logprobs_list;
  result["logprob"] = ftt.logprob;
  result["length"] = ftt.length;

  // Convert children recursively
  pybind11::list children_list;
  for (const FTT& child : ftt.get_children()) {
    children_list.append(convert_ftt_to_pydict(id, child));
  }
  result["children"] = children_list;

  // Add metadata
  result["pruned"] = ftt.pruned;
    
  return result;
}

}
