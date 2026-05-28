
#include "autocog/backend/llama/convert.hxx"
#include "autocog/utilities/errors.hxx"

#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"

#include "autocog/backend/llama/manager.hxx"

#include <map>

namespace autocog::backend::llama {

using namespace autocog::runtime::fta;

static void require_field(nlohmann::json const & j, std::string const & uid, char const * field) {
    if (!j.contains(field))
        throw autocog::ConfigError("FTA action '" + uid + "' missing required field '" + std::string(field) + "'", uid);
}

FTA convert_json_to_fta(ModelID const id, nlohmann::json const & jsondata) {
    Model & model = Manager::get_model(id);
    FTA fta;
    
    if (!jsondata.contains("actions")) {
        throw autocog::ConfigError("FTA JSON missing 'actions' field", "");
    }
    
    auto json_actions = jsondata["actions"];
    std::map<std::string, ActionID> uid_to_id;
    
    // First pass: create actions
    for (const auto& action_json : json_actions) {
        if (!action_json.contains("uid")) {
            throw autocog::ConfigError("Action missing 'uid' field", "");
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
            throw autocog::ConfigError("Action missing 'type' or '__type__' field: " + uid, uid);
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
                require_field(action_json, uid, "threshold");
                require_field(action_json, uid, "length");
                require_field(action_json, uid, "beams");
                require_field(action_json, uid, "ahead");
                require_field(action_json, uid, "width");
                require_field(action_json, uid, "stop_text");
                threshold = action_json["threshold"];
                length    = action_json["length"];
                beams     = action_json["beams"];
                ahead     = action_json["ahead"];
                width     = action_json["width"];
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
                std::string stop_str = action_json["stop_text"];
                completion_action->stop = model.tokenize(stop_str, false, true);
                
                // Full vocab mask (all tokens allowed)
                completion_action->vocab.mask.assign(model.vocab_size(), true);
            } else {
                if (!action_json.contains("stop")) {
                    throw autocog::ConfigError("Completion missing 'stop' field: " + uid, uid);
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
                require_field(action_json, uid, "threshold");
                require_field(action_json, uid, "width");
                threshold = action_json["threshold"];
                width     = action_json["width"];
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
            throw autocog::ConfigError("Unknown action type: " + action_type, action_type);
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
                    throw autocog::ConfigError("Unknown successor UID: " + succ_uid, succ_uid);
                }
                action->successors.push_back(uid_to_id[succ_uid]);
            }
        }
    }
    
    return fta;
}

nlohmann::json convert_ftt_to_json(ModelID const id, nlohmann::json const & fta_json,
                                   FTA const & fta, FTT const & ftt) {
    Model & model = Manager::get_model(id);
    auto const & fta_actions = fta_json["actions"];
    
    nlohmann::json result;
    
    // Action ID and UID
    result["action"] = ftt.action;
    if (ftt.action < fta.actions.size()) {
        result["uid"] = fta.actions[ftt.action]->name;
        // Copy field metadata from FTA JSON if present
        auto const & aj = fta_actions[ftt.action];
        if (aj.contains("field")) result["field"] = aj["field"];
        if (aj.contains("indices")) result["indices"] = aj["indices"];
    }
    
    // Detokenized text
    if (!ftt.tokens.empty()) {
        result["text"] = model.detokenize(ftt.tokens, false, false);
    } else {
        result["text"] = "";
    }
    
    // Logprobs
    result["logprob"] = ftt.logprob;
    nlohmann::json logprobs_array = nlohmann::json::array();
    for (float lpb : ftt.logprobs) {
        logprobs_array.push_back(lpb);
    }
    result["logprobs"] = logprobs_array;
    result["length"] = ftt.length;
    result["pruned"] = ftt.pruned;
    
    // Token IDs
    nlohmann::json token_array = nlohmann::json::array();
    for (TokenID token : ftt.tokens) {
        token_array.push_back(token);
    }
    result["tokens"] = token_array;
    
    // Children
    nlohmann::json children_array = nlohmann::json::array();
    for (const FTT& child : ftt.get_children()) {
        children_array.push_back(convert_ftt_to_json(id, fta_json, fta, child));
    }
    result["children"] = children_array;
    
    return result;
}

} // namespace autocog::backend::llama
