#include "autocog/codec/json.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <>
nlohmann::json to_json(Metadata const & m) {
  return nlohmann::json{
    {"format",    m.format},
    {"version",   m.version},
    {"hash",      m.hash},
    {"timestamp", m.timestamp},
  };
}

template <>
void from_json(nlohmann::json const & dom, Metadata & m) {
  autocog::codec::read_guarded("Metadata", [&]{
  m.format    = dom.at("format").get<std::string>();
  m.version   = dom.at("version").get<std::string>();
  m.hash      = dom.at("hash").get<std::string>();
  m.timestamp = dom.at("timestamp").get<std::string>();
  });
}

}
