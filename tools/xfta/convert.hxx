#ifndef TOOLS_XFTA_CONVERT_HXX
#define TOOLS_XFTA_CONVERT_HXX

#include "autocog/llama/xfta/fta.hxx"
#include "autocog/llama/xfta/ftt.hxx"

#include <nlohmann/json.hpp>

namespace autocog::llama::xfta {

FTA convert_json_to_fta(ModelID const id, nlohmann::json const & jsondata);
nlohmann::json convert_ftt_to_json(ModelID const id, FTT const & ftt);

}

#endif /* TOOLS_XFTA_CONVERT_HXX */
