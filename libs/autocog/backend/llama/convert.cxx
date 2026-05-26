
#include "autocog/backend/llama/convert.hxx"

#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"

#include "autocog/backend/llama/manager.hxx"

#include <map>

namespace autocog::backend::llama {

using namespace autocog::runtime::fta;

FTA convert_json_to_fta(ModelID const id, nlohmann::json const & jsondata) {
    Model & model = Manager::get_model(id);
    FTA fta;
    
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
        
        // Detect format: "type" (text-level from ista) vs "__type__" (tokenized)
        std::string action_type;
        bool text_level = false;
        if (action_json.contains("type")) {
            action_type = action_json["type"];
            text_level = true;
        } else if (action_json.contains("__type__")) {
            action_type = action_json["__type__"];
        } else {
            throw std::runtime_error("Action missing 'type' or '__type__' field: " + uid);
        }
        
        ActionID node_id = fta.actions.size();
        uid_to_id[uid] = node_id;
        
        std::unique_ptr<Action> action;
        
        if (action_type == "text" || action_type == "Text") {
            bool evaluate = false;
            if (action_json.contains("evaluate")) {
                evaluate = action_json["evaluate"];
            }
            
            action = std::make_unique<Text>(node_id, uid, evaluate);
            Text* text_action = static_cast<Text*>(action.get());
            
            if (text_level && action_json.contains("text")) {
                // Text-level: tokenize the string
                std::string text = action_json["text"];
                text_action->tokens = model.tokenize(text, false, true);
            } else if (action_json.contains("tokens")) {
                // Tokenized: use directly
                for (const auto& token : action_json["tokens"]) {
                    text_action->tokens.push_back(token.get<TokenID>());
                }
            }
            
        } else if (action_type == "complete" || action_type == "Complete") {
            float threshold;
            unsigned length, beams, ahead, width;
            std::optional<float> repetition = std::nullopt;
            std::optional<float> diversity = std::nullopt;

            if (text_level) {
                // Text-level: use provided values or defaults
                threshold = action_json.value("threshold", 0.1f);
                length    = action_json.value("length", 50u);
                beams     = action_json.value("beams", 4u);
                ahead     = action_json.value("ahead", 2u);
                width     = action_json.value("width", 4u);
            } else {
                threshold = action_json["threshold"];
                length    = action_json["length"];
                beams     = action_json["beams"];
                ahead     = action_json["ahead"];
                width     = action_json["width"];
            }

            if (action_json.contains("repetition") && !action_json["repetition"].is_null()) {
                repetition = action_json["repetition"];
            }
            if (action_json.contains("diversity") && !action_json["diversity"].is_null()) {
                diversity = action_json["diversity"];
            }
            
            action = std::make_unique<Completion>(node_id, uid, threshold, length, 
                                                  beams, ahead, width, repetition, diversity);
            Completion* completion_action = static_cast<Completion*>(action.get());

            if (text_level) {
                // Tokenize stop string (default: newline)
                std::string stop_str = action_json.value("stop_text", "\n");
                completion_action->stop = model.tokenize(stop_str, false, true);
                
                // Full vocab mask (all tokens allowed)
                completion_action->vocab.mask.assign(model.vocab_size(), true);
            } else {
                if (!action_json.contains("stop")) {
                    throw std::runtime_error("Completion missing 'stop' field: " + uid);
                }
                for (const auto& token : action_json["stop"]) {
                    completion_action->stop.push_back(token.get<TokenID>());
                }
                completion_action->vocab.mask.clear();
                completion_action->vocab.mask.reserve(model.vocab_size());
                for (const auto& tok_msk : action_json["vocab"]) {
                    completion_action->vocab.mask.push_back(tok_msk.get<bool>());
                }
            }
            
        } else if (action_type == "choose" || action_type == "Choose") {
            float threshold;
            unsigned width;

            if (text_level) {
                threshold = action_json.value("threshold", 0.1f);
                width     = action_json.value("width", 4u);
            } else {
                threshold = action_json["threshold"];
                width     = action_json["width"];
            }
            
            action = std::make_unique<Choice>(node_id, uid, threshold, width);
            Choice* choice_action = static_cast<Choice*>(action.get());
            
            if (action_json.contains("choices")) {
                choice_action->choices.clear();
                choice_action->choices.reserve(action_json["choices"].size());
                
                for (const auto& choice_json : action_json["choices"]) {
                    if (text_level && choice_json.is_string()) {
                        // Text-level: tokenize choice string
                        auto tokens = model.tokenize(choice_json.get<std::string>(), false, true);
                        choice_action->choices.push_back(std::move(tokens));
                    } else {
                        // Tokenized: array of token IDs
                        TokenSequence choice_tokens;
                        choice_tokens.reserve(choice_json.size());
                        for (const auto& token : choice_json) {
                            choice_tokens.push_back(token.get<TokenID>());
                        }
                        choice_action->choices.push_back(std::move(choice_tokens));
                    }
                }
            }
            
        } else {
            throw std::runtime_error("Unknown action type: " + action_type);
        }
        
        fta.actions.push_back(std::move(action));
    }
    
    // Second pass: add successors
    for (const auto& action_json : json_actions) {
        std::string uid = action_json["uid"];
        ActionID node_id = uid_to_id[uid];
        Action* action = fta.actions[node_id].get();
        
        if (action_json.contains("successors")) {
            for (const auto& successor_uid : action_json["successors"]) {
                std::string succ_uid = successor_uid;
                if (uid_to_id.find(succ_uid) == uid_to_id.end()) {
                    throw std::runtime_error("Unknown successor UID: " + succ_uid);
                }
                action->successors.push_back(uid_to_id[succ_uid]);
            }
        }
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

} // namespace autocog::backend::llama
