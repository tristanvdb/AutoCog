#ifndef __AUTOCOG_LLAMA_TYPES__HXX_
#define __AUTOCOG_LLAMA_TYPES__HXX_

#include <llama.h>
#include <vector>

namespace autocog {
namespace llama {

using ContextID = unsigned;
using NodeID = unsigned;
using TokenID = llama_token;

using TokenSequence = std::vector<TokenID>;

} }

#endif /* __AUTOCOG_LLAMA_TYPES__HXX_ */

