#ifndef AUTOCOG_RUNTIME_FTA_TYPES_HXX
#define AUTOCOG_RUNTIME_FTA_TYPES_HXX

#include <cstdint>
#include <vector>


namespace autocog::runtime::fta {

using EvalID = unsigned;
using ActionID = unsigned;
using TokenID = int32_t;

using TokenSequence = std::vector<TokenID>;
using ProbaSequence = std::vector<float>;

}

#endif // AUTOCOG_RUNTIME_FTA_TYPES_HXX
