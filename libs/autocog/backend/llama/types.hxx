#ifndef AUTOCOG_BACKEND_LLAMA_TYPES_HXX
#define AUTOCOG_BACKEND_LLAMA_TYPES_HXX

#include "autocog/runtime/fta/types.hxx"

#include <llama.h>

namespace autocog::backend::llama {

// Type aliases for runtime FTA types (re-exported for convenience)
using EvalID        = autocog::runtime::fta::EvalID;
using ActionID      = autocog::runtime::fta::ActionID;
using TokenID       = autocog::runtime::fta::TokenID;
using TokenSequence = autocog::runtime::fta::TokenSequence;
using ProbaSequence = autocog::runtime::fta::ProbaSequence;

// Backend-specific types
using ModelID = unsigned;
using ContextID = unsigned;

}

#endif // AUTOCOG_BACKEND_LLAMA_TYPES_HXX
