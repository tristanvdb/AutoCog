
#include "convert.hxx"

#include "autocog/llama/xfta/manager.hxx"

#include <map>

namespace autocog::llama::xfta {

FTA convert_json_to_fta(ModelID const id, nlohmann::json const & jsondata) {
    [[maybe_unused]] Model & model = Manager::get_model(id);
    FTA fta;
    
    // Get the actions array from JSON FTA
    if (!jsondata.contains("actions")) {
        throw std::runtime_error("FTA JSON missing 'actions' field");
    }
    
    auto json_actions = jsondata["actions"];
    std::map<std::string, ActionID> uid_to_id;
    
    // First pass: create actions
    for (const auto& action_json : json_actions) {
        if (!action_json.contains("uid")) {
            throw std::runtime_error("Action missing 'uid' field");
        }
        std::string uid = action_json["uid"];
        
        if (!action_json.contains("__type__")) {
            throw std::runtime_error("Action missing '__type__' field: " + uid);
        }
        std::string action_type = action_json["__type__"];
        
        ActionID node_id = fta.actions.size(); // Assign sequential IDs
        uid_to_id[uid] = node_id;
        
        std::unique_ptr<Action> action;
        
        if (action_type == "Text") {
            bool evaluate = false;
            if (action_json.contains("evaluate")) {
                evaluate = action_json["evaluate"];
            }
            
            action = std::make_unique<Text>(node_id, uid, evaluate);
            Text* text_action = static_cast<Text*>(action.get());
            
            // Convert tokens array
            text_action->tokens.clear();
            for (const auto& token : action_json["tokens"]) {
                text_action->tokens.push_back(token.get<TokenID>());
            }
            
        } else if (action_type == "Complete") {
            float threshold = action_json["threshold"];
            unsigned length = action_json["length"];
            unsigned beams = action_json["beams"];
            unsigned ahead = action_json["ahead"];
            unsigned width = action_json["width"];
            
            std::optional<float> repetition = std::nullopt;
            if (action_json.contains("repetition") && !action_json["repetition"].is_null()) {
                repetition = action_json["repetition"];
            }
            
            std::optional<float> diversity = std::nullopt;
            if (action_json.contains("diversity") && !action_json["diversity"].is_null()) {
                diversity = action_json["diversity"];
            }
            
            action = std::make_unique<Completion>(node_id, uid, threshold, length, 
                                                  beams, ahead, width, repetition, diversity);
            Completion* completion_action = static_cast<Completion*>(action.get());
            
            // Process stop tokens
            if (!action_json.contains("stop")) {
                throw std::runtime_error("Completion missing 'stop' field: " + uid);
            }
            completion_action->stop.clear();
            for (const auto& token : action_json["stop"]) {
                completion_action->stop.push_back(token.get<TokenID>());
            }
            
            // Process vocabulary mask
            completion_action->vocab.mask.clear();
            completion_action->vocab.mask.reserve(model.vocab_size());
            for (const auto& tok_msk : action_json["vocab"]) {
                completion_action->vocab.mask.push_back(tok_msk.get<bool>());
            }
            
        } else if (action_type == "Choose") {
            float threshold = action_json["threshold"];
            unsigned width = action_json["width"];
            
            action = std::make_unique<Choice>(node_id, uid, threshold, width);
            Choice* choice_action = static_cast<Choice*>(action.get());
            
            // Process choices array
            if (action_json.contains("choices")) {
                choice_action->choices.clear();
                choice_action->choices.reserve(action_json["choices"].size());
                
                for (const auto& choice_json : action_json["choices"]) {
                    TokenSequence choice_tokens;
                    choice_tokens.reserve(choice_json.size());
                    for (const auto& token : choice_json) {
                        choice_tokens.push_back(token.get<TokenID>());
                    }
                    choice_action->choices.push_back(std::move(choice_tokens));
                }
            }
            
        } else {
            throw std::runtime_error("Unknown action type: " + action_type);
        }
        
        fta.actions.push_back(std::move(action));
    }
    
    // Second pass: add successors
    size_t idx = 0;
    for (const auto& action_json : json_actions) {
        std::string uid = action_json["uid"];
        ActionID node_id = uid_to_id[uid];
        Action* action = fta.actions[node_id].get();
        
        // Add successors
        if (action_json.contains("successors")) {
            for (const auto& successor_uid : action_json["successors"]) {
                std::string succ_uid = successor_uid;
                if (uid_to_id.find(succ_uid) == uid_to_id.end()) {
                    throw std::runtime_error("Unknown successor UID: " + succ_uid);
                }
                action->successors.push_back(uid_to_id[succ_uid]);
            }
        }
        idx++;
    }
    
    return fta;
}

nlohmann::json convert_ftt_to_json(ModelID const id, FTT const & ftt) {
    Model & model = Manager::get_model(id);
    
    nlohmann::json result;
    
    // Basic fields
    result["action"] = ftt.action;
    
    // Convert tokens
    nlohmann::json token_array = nlohmann::json::array();
    for (TokenID token : ftt.tokens) {
        token_array.push_back(token);
    }
    result["tokens"] = token_array;
    
    // Convert probabilities
    nlohmann::json logprobs_array = nlohmann::json::array();
    for (float lpb : ftt.logprobs) {
        logprobs_array.push_back(lpb);
    }
    result["logprobs"] = logprobs_array;
    result["logprob"] = ftt.logprob;
    result["length"] = ftt.length;
    
    // Convert children recursively
    nlohmann::json children_array = nlohmann::json::array();
    for (const FTT& child : ftt.get_children()) {
        children_array.push_back(convert_ftt_to_json(id, child));
    }
    result["children"] = children_array;
    
    // Add metadata
    result["pruned"] = ftt.pruned;
    
    // Optional: Add text representation of tokens for debugging
    if (!ftt.tokens.empty()) {
        result["text"] = model.detokenize(ftt.tokens, false, false);
    }
    
    return result;
}

} // namespace autocog::llama::xfta
