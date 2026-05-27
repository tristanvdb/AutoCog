#ifndef AUTOCOG_RUNTIME_STA_INSTANTIATE_HXX
#define AUTOCOG_RUNTIME_STA_INSTANTIATE_HXX

#include "autocog/runtime/sta/state.hxx"
#include "autocog/runtime/sta/syntax.hxx"
#include "autocog/runtime/sta/search.hxx"
#include <nlohmann/json.hpp>

namespace autocog::runtime::sta {

// Instantiate a prompt into a text-level FTA.
// content: pre-resolved channel data as JSON object
// search: search config (beams, width, etc.) embedded into FTA actions
// Returns: FTA as JSON {"actions": [...]}
nlohmann::json instantiate(
    PromptSTA const & prompt,
    nlohmann::json const & content,
    Syntax const & syntax,
    SearchConfig const & search
);

// Render FTA as human-readable text (for --text mode)
std::string render_text(nlohmann::json const & fta);

}

#endif // AUTOCOG_RUNTIME_STA_INSTANTIATE_HXX
