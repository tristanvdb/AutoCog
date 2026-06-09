#ifndef AUTOCOG_CODEC_PYTHON_HXX
#define AUTOCOG_CODEC_PYTHON_HXX

// Free-function pybind conversion for autocog::data artifacts.
//
// DRAFT layout -- for review. Conversion lives here, NOT on the types, so the
// artifact headers carry no pybind dependency and the C++ tools never pull it
// in. Only the binding modules include this header. Implementations are split
// per type into data/<x>-python.cxx (dash, not dot).

// pybind headers first: stl.h must define the std::vector/std::map casters
// before the artifact type headers are processed, so every -python.cxx that
// includes this header sees them at its conversion sites.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "autocog/data/metadata.hxx"
#include "autocog/data/syntax.hxx"
#include "autocog/data/search.hxx"
#include "autocog/data/vocab.hxx"
#include "autocog/data/ftt.hxx"
#include "autocog/data/fta.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/utilities/types.hxx"

#include <memory>

namespace autocog::codec {

// Serialize: deduces T, returns a new Python object. Specialized per type.
template <class T> pybind11::object to_py(T const & obj);

template <> pybind11::object to_py(autocog::data::Metadata const &);
template <> pybind11::object to_py(autocog::data::Syntax const &);
template <> pybind11::object to_py(autocog::data::SearchConfig const &);
template <> pybind11::object to_py(autocog::data::VocabExpr const &);
template <> pybind11::object to_py(autocog::data::FTT const &);
template <> pybind11::object to_py(autocog::data::FTA const &);
template <> pybind11::object to_py(autocog::data::STA const &);

// Shared sub-structs used across data translation units.
template <> pybind11::object to_py(autocog::data::PathStep const &);
template <> pybind11::object to_py(autocog::data::Channel const &);

// Deserialize workhorse: fills in place (no per-sub-struct move). Specialized
// per type; sub-structs get file-local specializations in their .cxx.
template <class T> void from_py(pybind11::object const & obj, T & out);

template <> void from_py(pybind11::object const &, autocog::data::Metadata &);
template <> void from_py(pybind11::object const &, autocog::data::Syntax &);
template <> void from_py(pybind11::object const &, autocog::data::SearchConfig &);
template <> void from_py(pybind11::object const &, autocog::data::VocabExpr &);
template <> void from_py(pybind11::object const &, autocog::data::FTT &);
template <> void from_py(pybind11::object const &, autocog::data::FTA &);
template <> void from_py(pybind11::object const &, autocog::data::STA &);

template <> void from_py(pybind11::object const &, autocog::data::PathStep &);
template <> void from_py(pybind11::object const &, autocog::data::Channel &);

template <> pybind11::object to_py(autocog::types::Document const &);
template <> void from_py(pybind11::object const &, autocog::types::Document &);

// Artifact boundary: construct, fill in place, finalize, return owned.
template <class T> std::unique_ptr<T> from_py(pybind11::object const & obj) {
    auto out = std::make_unique<T>();
    from_py(obj, *out);
    out->finalize();
    return out;
}

}

#endif // AUTOCOG_CODEC_PYTHON_HXX
