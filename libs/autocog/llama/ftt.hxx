#ifndef __AUTOCOG_LLAMA_FTT__HXX_
#define __AUTOCOG_LLAMA_FTT__HXX_

#include "autocog/llama/types.hxx"

#include <pybind11/pybind11.h>

namespace autocog {
namespace llama {

struct FTT {
  ActionID const action;
  TokenSequence const tokens;
  ProbaSequence const probas;
  float const probability;

  std::vector<FTT> children;
  bool pruned{false};

  FTT(ActionID const action_, TokenSequence const & tokens_, ProbaSequence const & probas_, float probability_);

  FTT & add(ActionID const action_, TokenSequence const & tokens_, ProbaSequence const & probas_, float probability_);

  pybind11::dict pydict() const;

  
};

} }

#endif /* __AUTOCOG_LLAMA_FTT__HXX_ */

