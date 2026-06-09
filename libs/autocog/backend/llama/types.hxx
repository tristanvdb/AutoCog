#ifndef AUTOCOG_BACKEND_LLAMA_TYPES_HXX
#define AUTOCOG_BACKEND_LLAMA_TYPES_HXX

#include <cstdint>
#include <vector>

#include <llama.h>

namespace autocog::backend::llama {

// Numeric generation types (layout-compatible with autocog::data's TokenID /
// ActionID, so an FTT node's tokens/action assign directly).
using EvalID        = unsigned;
using ActionID      = unsigned;
using TokenID       = std::int32_t;
using TokenSequence = std::vector<TokenID>;
using ProbaSequence = std::vector<float>;

// Backend-specific types
using ModelID   = unsigned;
using ContextID = unsigned;

}

#endif // AUTOCOG_BACKEND_LLAMA_TYPES_HXX
