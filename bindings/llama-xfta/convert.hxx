#ifndef BINDINGS_LLAMA_XFTA_CONVERT_HXX
#define BINDINGS_LLAMA_XFTA_CONVERT_HXX

#include "autocog/llama/xfta/fta.hxx"
#include "autocog/llama/xfta/ftt.hxx"

#include <pybind11/pybind11.h>

namespace autocog::llama::xfta {

FTA convert_pydict_to_fta(ModelID const id, pybind11::dict const & pydata);
pybind11::dict convert_ftt_to_pydict(ModelID const id, FTT const & ftt);

}

#endif /* BINDINGS_LLAMA_XFTA_CONVERT_HXX */
