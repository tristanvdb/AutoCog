#include "autocog/codec/python.hxx"
#include "autocog/utilities/types.hxx"

#include <type_traits>

namespace autocog::codec {
namespace py = pybind11;

template <> py::object to_py(autocog::types::Document const & doc) {
  using D = autocog::types::Document;
  if (auto const * v = std::get_if<autocog::types::Value>(&doc.value)) {
    return std::visit([](auto const & x) -> py::object {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, std::monostate>) return py::none();
      else return py::cast(x);
    }, *v);
  }
  if (auto const * a = std::get_if<D::Array>(&doc.value)) {
    py::list lst;
    for (auto const & e : *a) lst.append(to_py(e));
    return lst;
  }
  py::dict d;
  for (auto const & [k, val] : std::get<D::Object>(doc.value)) d[py::str(k)] = to_py(val);
  return d;
}

template <> void from_py(py::object const & obj, autocog::types::Document & doc) {
  using D = autocog::types::Document;
  if (py::isinstance<py::dict>(obj)) {
    D::Object o;
    for (auto item : obj.cast<py::dict>()) {
      D child; from_py(py::reinterpret_borrow<py::object>(item.second), child);
      o.emplace(item.first.cast<std::string>(), std::move(child));
    }
    doc.value = std::move(o);
  } else if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
    D::Array a;
    for (auto e : obj) { D child; from_py(py::reinterpret_borrow<py::object>(e), child); a.push_back(std::move(child)); }
    doc.value = std::move(a);
  } else {
    autocog::types::Value v;
    if      (py::isinstance<py::bool_>(obj))  v = obj.cast<bool>();   // bool before int
    else if (py::isinstance<py::int_>(obj))   v = obj.cast<int>();
    else if (py::isinstance<py::float_>(obj)) v = obj.cast<float>();
    else if (py::isinstance<py::str>(obj))    v = obj.cast<std::string>();
    else                                      v = std::monostate{};
    doc.value = v;
  }
}

}
