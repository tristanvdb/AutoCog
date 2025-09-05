
#include "convert.hxx"

#include "autocog/llama/xfta/manager.hxx"

namespace autocog::llama::xfta {

FTA convert_json_to_fta(ModelID const id, nlohmann::json const & jsondata) {
  Model & model = Manager::get_model(id);

  FTA fta;

  // TODO

  return fta;
}

nlohmann::json convert_ftt_to_json(ModelID const id, FTT const & ftt) {
  Model & model = Manager::get_model(id);

  nlohmann::json result;

  // TODO

  return result;
}

}
