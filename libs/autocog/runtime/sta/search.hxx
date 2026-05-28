#ifndef AUTOCOG_RUNTIME_STA_SEARCH_HXX
#define AUTOCOG_RUNTIME_STA_SEARCH_HXX

#include <string>
#include <optional>
#include <fstream>
#include <nlohmann/json.hpp>
#include "autocog/utilities/errors.hxx"

namespace autocog::runtime::sta {

struct SearchConfig {
    struct CompletionDefaults {
        unsigned beams;
        unsigned ahead;
        unsigned width;
        float threshold;
        std::optional<float> repetition;
        std::optional<float> diversity;
        std::string stop;
    } completion;

    struct ChoiceDefaults {
        float threshold;
        unsigned width;
    } choice;

    struct TextDefaults {
        bool evaluate;
    } text;
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

    SearchConfig cfg;

    require("completion", "beams");
    require("completion", "ahead");
    require("completion", "width");
    require("completion", "threshold");
    require("completion", "stop");
    require("completion", "repetition");
    require("completion", "diversity");

    auto const & c = j["completion"];
    cfg.completion.beams     = c["beams"];
    cfg.completion.ahead     = c["ahead"];
    cfg.completion.width     = c["width"];
    cfg.completion.threshold = c["threshold"];
    cfg.completion.stop      = c["stop"];
    if (!c["repetition"].is_null()) cfg.completion.repetition = c["repetition"].get<float>();
    if (!c["diversity"].is_null())  cfg.completion.diversity  = c["diversity"].get<float>();

    require("choice", "threshold");
    require("choice", "width");

    auto const & ch = j["choice"];
    cfg.choice.threshold = ch["threshold"];
    cfg.choice.width     = ch["width"];

    require("text", "evaluate");

    cfg.text.evaluate = j["text"]["evaluate"];

    return cfg;
}

}

#endif
