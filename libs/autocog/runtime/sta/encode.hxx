#ifndef AUTOCOG_RUNTIME_STA_ENCODE_HXX
#define AUTOCOG_RUNTIME_STA_ENCODE_HXX

#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/utilities/types.hxx"

#include <string>

namespace autocog::runtime::sta {

/// The inverse of walk_ftt_to_frame: given an FTA (compiled under some
/// syntax) and a frame of field values, emit the FTT of the single path that
/// produces those values. Because a frame is syntax-independent, this is the
/// re-renderer of the trace pipeline: one recorded frame can be encoded under
/// any syntax the program compiles with — including syntaxes the generating
/// model has never seen.
///
/// The result is text-level: every node carries its action's text (fixed
/// text, the frame value for a completion, the matching choice for a value
/// choose), with empty tokens/logprobs. Token-level materialization is a
/// backend concern (autocog::backend::llama::tokenize), since tokens are
/// model-bound.
///
/// Decisions along the way:
///  - value chooses (schema field set) match the frame value against the
///    choice texts — frames here hold raw choice strings, as psta emits
///    (select indices unresolved);
///  - structural branch chooses (no field) take the first successor whose
///    nearest reachable value action has a value present in the frame — the
///    "continue the array" choice is first by construction, so arrays extend
///    exactly while the frame has elements.
///
/// Throws ConfigError when the frame is inconsistent with the FTA (a missing
/// completion value, a choice text matching no choice, no viable branch).
autocog::data::FTT encode_frame_to_ftt(
    autocog::data::FTA const & fta,
    autocog::data::STA const & sta,
    std::string const & prompt_name,
    autocog::types::Document const & frame
);

}

#endif // AUTOCOG_RUNTIME_STA_ENCODE_HXX
