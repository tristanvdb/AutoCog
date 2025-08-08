#ifndef __AUTOCOG_LLAMA_TYPES__HXX_
#define __AUTOCOG_LLAMA_TYPES__HXX_

#include <llama.h>
#include <vector>

#ifndef VERBOSE
#  define VERBOSE 0
#endif

namespace autocog {
namespace llama {

using EvalID = unsigned;
using ModelID = unsigned;
using ContextID = unsigned;
using ActionID = unsigned;
using TokenID = llama_token;

using TokenSequence = std::vector<TokenID>;
using ProbaSequence = std::vector<float>;

} }

#endif /* __AUTOCOG_LLAMA_TYPES__HXX_ */

