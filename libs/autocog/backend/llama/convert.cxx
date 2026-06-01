
#include "autocog/backend/llama/convert.hxx"
#include "autocog/utilities/errors.hxx"

#include "autocog/runtime/fta/fta.hxx"
#include "autocog/runtime/fta/ftt.hxx"
#include "autocog/runtime/fta/vocab.hxx"

#include "autocog/backend/llama/manager.hxx"

#include <map>
#include <regex>

namespace autocog::backend::llama {

using namespace autocog::runtime::fta;

static void require_field(nlohmann::json const & j, std::string const & uid, char const * field) {
    if (!j.contains(field))
        throw autocog::ConfigError("FTA action '" + uid + "' missing required field '" + std::string(field) + "'", uid);
}

// Build a token mask (one bool per vocab id) from a resolved vocab expression.
//   tokenize(s, ...): the set of token ids that appear in the tokenization of
//                     each string (union). With a character-level vocab,
//                     tokenize("foo") = {'f','o'}.
//   regex(p):         token ids whose surface (detokenized) matches the regex.
//   union/intersect/diff/complement: set algebra over the masks.
static std::vector<bool> build_vocab_mask(Model & model, VocabExpr const & ve) {
    size_t const n = model.vocab_size();
    switch (ve.kind) {
        case VocabExpr::Kind::Tokenize: {
            std::vector<bool> m(n, false);
            for (auto const & s : ve.strings) {
                for (TokenID t : model.tokenize(s, false, true)) {
                    if (t >= 0 && static_cast<size_t>(t) < n) m[t] = true;
                }
            }
            return m;
        }
        case VocabExpr::Kind::Regex: {
            std::vector<bool> m(n, false);
            std::regex re(ve.strings.empty() ? std::string{} : ve.strings[0]);
            for (size_t t = 0; t < n; ++t) {
                std::string surface = model.detokenize({static_cast<TokenID>(t)}, false, false);
                if (std::regex_search(surface, re)) m[t] = true;
            }
            return m;
        }
        case VocabExpr::Kind::Union: {
            auto a = build_vocab_mask(model, *ve.operands[0]);
            auto b = build_vocab_mask(model, *ve.operands[1]);
            for (size_t i = 0; i < n; ++i) a[i] = a[i] || b[i];
            return a;
        }
        case VocabExpr::Kind::Intersect: {
            auto a = build_vocab_mask(model, *ve.operands[0]);
            auto b = build_vocab_mask(model, *ve.operands[1]);
            for (size_t i = 0; i < n; ++i) a[i] = a[i] && b[i];
            return a;
        }
        case VocabExpr::Kind::Diff: {
            auto a = build_vocab_mask(model, *ve.operands[0]);
            auto b = build_vocab_mask(model, *ve.operands[1]);
            for (size_t i = 0; i < n; ++i) a[i] = a[i] && !b[i];
            return a;
        }
        case VocabExpr::Kind::Complement: {
            auto a = build_vocab_mask(model, *ve.operands[0]);
            a.flip();
            return a;
        }
    }
    return std::vector<bool>(n, true);
}

FTA convert_json_to_fta(ModelID const id, nlohmann::json const & jsondata) {
    Model & model = Manager::get_model(id);
    FTA fta;
    
    if (!jsondata.contains("actions")) {
        throw autocog::ConfigError("FTA JSON missing 'actions' field", "");
    }

    // Vocab table (vocab_<hash> -> expression tree). Each referenced entry is
    // turned into a token mask, built once and cached per (model, hash).
    nlohmann::json const empty_vocabs = nlohmann::json::object();
    nlohmann::json const & vocabs = jsondata.contains("vocabs") ? jsondata["vocabs"] : empty_vocabs;
    std::map<std::string, std::vector<bool>> mask_cache;

    auto vocab_mask = [&](std::string const & ref) -> std::vector<bool> {
        auto cit = mask_cache.find(ref);
        if (cit != mask_cache.end()) return cit->second;
        if (!vocabs.contains(ref)) {
            throw autocog::ConfigError("Completion references unknown vocab '" + ref + "'", ref);
        }
        auto ve = runtime::fta::vocab_from_json(vocabs[ref]);
        std::vector<bool> m = build_vocab_mask(model, *ve);
        mask_cache.emplace(ref, m);
        return m;
    };
    
    auto json_actions = jsondata["actions"];
    std::map<std::string, ActionID> uid_to_id;
    
    // First pass: create actions
    for (const auto& action_json : json_actions) {
        if (!action_json.contains("uid")) {
            throw autocog::ConfigError("Action missing 'uid' field", "");
        }
        std::string uid = action_json["uid"];

        if (!action_json.contains("type")) {
            throw autocog::ConfigError("Action missing 'type' field: " + uid, uid);
        }
        std::string action_type = action_json["type"];
        
        ActionID node_id = fta.actions.size();
        uid_to_id[uid] = node_id;
        
        std::unique_ptr<Action> action;
        
        if (action_type == "text") {
            bool evaluate = false;
            if (action_json.contains("evaluate")) {
                evaluate = action_json["evaluate"];
            }
            
            action = std::make_unique<Text>(node_id, uid, evaluate);
            Text* text_action = static_cast<Text*>(action.get());
            
            std::string text = action_json.value("text", std::string{});
            if (!text.empty()) text_action->tokens = model.tokenize(text, false, true);
            
        } else if (action_type == "complete") {
            require_field(action_json, uid, "threshold");
            require_field(action_json, uid, "length");
            require_field(action_json, uid, "beams");
            require_field(action_json, uid, "ahead");
            require_field(action_json, uid, "width");
            require_field(action_json, uid, "stop_text");
            float threshold  = action_json["threshold"];
            unsigned length  = action_json["length"];
            unsigned beams   = action_json["beams"];
            unsigned ahead   = action_json["ahead"];
            unsigned width   = action_json["width"];
            std::optional<float> repetition = std::nullopt;
            std::optional<float> diversity = std::nullopt;
            if (action_json.contains("repetition") && !action_json["repetition"].is_null()) {
                repetition = action_json["repetition"];
            }
            if (action_json.contains("diversity") && !action_json["diversity"].is_null()) {
                diversity = action_json["diversity"];
            }
            
            action = std::make_unique<Completion>(node_id, uid, threshold, length, 
                                                  beams, ahead, width, repetition, diversity);
            Completion* completion_action = static_cast<Completion*>(action.get());

            std::string stop_str = action_json["stop_text"];
            completion_action->stop = model.tokenize(stop_str, false, true);

            // Vocab: build the token mask from the referenced expression tree;
            // absent reference means unrestricted (all tokens allowed).
            if (action_json.contains("vocab") && !action_json["vocab"].is_null()) {
                completion_action->vocab.mask = vocab_mask(action_json["vocab"].get<std::string>());
            } else {
                completion_action->vocab.mask.assign(model.vocab_size(), true);
            }
            
        } else if (action_type == "choose") {
            require_field(action_json, uid, "threshold");
            require_field(action_json, uid, "width");
            float threshold = action_json["threshold"];
            unsigned width  = action_json["width"];
            
            action = std::make_unique<Choice>(node_id, uid, threshold, width);
            Choice* choice_action = static_cast<Choice*>(action.get());
            
            if (action_json.contains("choices")) {
                choice_action->choices.clear();
                choice_action->choices.reserve(action_json["choices"].size());
                for (const auto& choice_json : action_json["choices"]) {
                    auto tokens = model.tokenize(choice_json.get<std::string>(), false, true);
                    choice_action->choices.push_back(std::move(tokens));
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
