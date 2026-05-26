#ifndef AUTOCOG_LLAMA_XFTA_TYPES_HXX
#define AUTOCOG_LLAMA_XFTA_TYPES_HXX

#include <llama.h>
#include <vector>

#ifndef VERBOSE
#  define VERBOSE 0
#endif

namespace autocog::llama::xfta {

using EvalID = unsigned;
using ModelID = unsigned;
using ContextID = unsigned;
using ActionID = unsigned;
using TokenID = llama_token;

using TokenSequence = std::vector<TokenID>;
using ProbaSequence = std::vector<float>;

}

#endif /* AUTOCOG_LLAMA_XFTA_TYPES_HXX */

