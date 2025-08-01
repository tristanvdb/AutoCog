#ifndef __AUTOCOG_LLAMA_FTT__HXX_
#define __AUTOCOG_LLAMA_FTT__HXX_

#include "autocog/llama/types.hxx"

#include <pybind11/pybind11.h>

namespace autocog {
namespace llama {

class FTT {
  public:
    FTT() = default;
    pybind11::dict pydict() const;

  private:
    std::vector<TokenID> tokens;
    std::vector<float> probas;
    std::vector<FTT> children;
};

} }

#endif /* __AUTOCOG_LLAMA_FTT__HXX_ */

