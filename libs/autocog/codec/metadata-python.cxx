#include "autocog/codec/python.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <>
pybind11::object to_py(Metadata const & m) {
  namespace py = pybind11;
  py::dict d;
  d["format"]    = m.format;
  d["version"]   = m.version;
  d["hash"]      = m.hash;
  d["timestamp"] = m.timestamp;
  return d;
}

template <>
void from_py(pybind11::object const & obj, Metadata & m) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  m.format    = d["format"].cast<std::string>();
  m.version   = d["version"].cast<std::string>();
  m.hash      = d["hash"].cast<std::string>();
  m.timestamp = d["timestamp"].cast<std::string>();
}

}
