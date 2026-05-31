#ifndef AUTOCOG_RUNTIME_STA_SEARCH_HXX
#define AUTOCOG_RUNTIME_STA_SEARCH_HXX

#include <string>
#include <optional>
#include <fstream>
#include <nlohmann/json.hpp>
#include "autocog/utilities/errors.hxx"

namespace autocog::runtime::sta {

// Typed per-category search parameters. These mirror exactly what xfta
// implements for each action type, and are used BOTH as the concrete config
// defaults (SearchConfig) and as the resolved per-action values stamped onto
// the FTA. The authoring-side policy (the open category->param map carried from
// the IR/STA) is resolved against these: for each struct field, the policy
// value wins if present, otherwise the config default is used.

// Parameters of a `complete` (text) action.
struct TextSearch {
    float threshold;
    unsigned beams;
    unsigned ahead;
    unsigned width;
    std::optional<float> repetition;
    std::optional<float> diversity;
};

// Parameters of a `choose` action. The same shape governs the three kinds of
// choose: enum-content (enum.*), record-successor branching (branch.*), and the
// flow / "next" choice (flow.*).
struct ChoiceSearch {
    float threshold;
    unsigned width;
};

// Queue-scope parameters (prompt-global). Carried through to the FTA; not yet
// consumed by the current xfta queue. TODO(xfta-queue).
struct QueueSearch {
    std::string metric;
};

struct SearchConfig {
    TextSearch   text;
    ChoiceSearch enums;    // `enum` is a keyword; this is the enum.* category
    ChoiceSearch branch;
    ChoiceSearch flow;
    QueueSearch  queue;
};

inline SearchConfig load_search_config(std::string const & path) {
    using json = nlohmann::json;

    std::ifstream f(path);
    if (!f) throw autocog::ConfigError("Cannot read search config: " + path, path);
    auto j = json::parse(f);

    auto require = [&](char const * section, char const * key) {
        if (!j.contains(section) || !j[section].contains(key))
            throw autocog::ConfigError(
                std::string("Search config missing required field: ") + section + "." + key, path);
    };

    auto load_choice = [&](char const * section) -> ChoiceSearch {
        require(section, "threshold");
        require(section, "width");
        ChoiceSearch c;
        c.threshold = j[section]["threshold"];
        c.width     = j[section]["width"];
        return c;
    };

    SearchConfig cfg;

    require("text", "threshold");
    require("text", "beams");
    require("text", "ahead");
    require("text", "width");
    auto const & t = j["text"];
    cfg.text.threshold = t["threshold"];
    cfg.text.beams     = t["beams"];
    cfg.text.ahead     = t["ahead"];
    cfg.text.width     = t["width"];
    if (t.contains("repetition") && !t["repetition"].is_null()) cfg.text.repetition = t["repetition"].get<float>();
    if (t.contains("diversity")  && !t["diversity"].is_null())  cfg.text.diversity  = t["diversity"].get<float>();

    cfg.enums  = load_choice("enum");
    cfg.branch = load_choice("branch");
    cfg.flow   = load_choice("flow");

    require("queue", "metric");
    cfg.queue.metric = j["queue"]["metric"];

    return cfg;
}

}

#endif
