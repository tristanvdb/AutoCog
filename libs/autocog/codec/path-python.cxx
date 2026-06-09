#include "autocog/codec/python.hxx"

namespace autocog::codec {
using namespace autocog::data;

template <>
pybind11::object to_py(PathStep const & ps) {
  namespace py = pybind11;
  py::dict d;
  d["name"] = ps.name;
  if (ps.selector) {
    if (std::holds_alternative<int>(*ps.selector)) {
      d["index"] = std::get<int>(*ps.selector);
    } else {
      auto const & r = std::get<StepRange>(*ps.selector);
      py::list sl;
      if (r.lower) sl.append(*r.lower); else sl.append(py::none());
      if (r.upper) sl.append(*r.upper); else sl.append(py::none());
      d["slice"] = sl;
    }
  }
  return d;
}

template <>
void from_py(pybind11::object const & obj, PathStep & ps) {
  namespace py = pybind11;
  py::dict d = obj.cast<py::dict>();
  ps.name = d["name"].cast<std::string>();
  ps.selector.reset();
  if (d.contains("index") && !d["index"].is_none()) {
    ps.selector = d["index"].cast<int>();
  } else if (d.contains("slice") && !d["slice"].is_none()) {
    StepRange r;
    py::list sl = d["slice"].cast<py::list>();
    if (!sl[0].is_none()) r.lower = sl[0].cast<int>();
    if (!sl[1].is_none()) r.upper = sl[1].cast<int>();
    ps.selector = r;
  }
}

}
