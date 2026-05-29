#ifndef AUTOCOG_RUNTIME_STA_LOAD_HXX
#define AUTOCOG_RUNTIME_STA_LOAD_HXX

#include "autocog/runtime/sta/state.hxx"
#include "autocog/runtime/sta/channel.hxx"
#include <nlohmann/json.hpp>

namespace autocog::runtime::sta {

PromptSTA load_prompt(nlohmann::json const & j);
Program load_program(nlohmann::json const & j);

nlohmann::json serialize_program(Program const & prog,
                                 std::string const & source_uid = "");
nlohmann::json serialize_prompt(PromptSTA const & prompt);

}

#endif // AUTOCOG_RUNTIME_STA_LOAD_HXX
