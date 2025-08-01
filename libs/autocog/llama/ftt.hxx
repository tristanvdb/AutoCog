#ifndef __AUTOCOG_LLAMA_FTT__HXX_
#define __AUTOCOG_LLAMA_FTT__HXX_

#include <vector>
#include <optional>

namespace autocog {
namespace llama {

using TokenID = unsigned;
using probability_t = float;

class FTT {
  public:
    FTT() = default;

  private:
    std::vector<TokenID> tokens;
    std::vector<probability_t> probas;
    std::vector<FTT> children;
};

} }

#endif /* __AUTOCOG_LLAMA_FTT__HXX_ */

