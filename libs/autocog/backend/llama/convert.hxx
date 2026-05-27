#ifndef AUTOCOG_BACKEND_LLAMA_CONVERT_HXX
#define AUTOCOG_BACKEND_LLAMA_CONVERT_HXX

#include "autocog/backend/llama/types.hxx"

#include <nlohmann/json.hpp>

namespace autocog::runtime::fta { class FTA; class FTT; }

namespace autocog::backend::llama {

runtime::fta::FTA convert_json_to_fta(
    ModelID const id, nlohmann::json const & jsondata);
nlohmann::json convert_ftt_to_json(
    ModelID const id, nlohmann::json const & fta_json,
    runtime::fta::FTA const & fta, runtime::fta::FTT const & ftt);

}

#endif // AUTOCOG_BACKEND_LLAMA_CONVERT_HXX
