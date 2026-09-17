#ifndef AUTOCOG_RUNTIME_FTA_SIMPLIFY_HXX
#define AUTOCOG_RUNTIME_FTA_SIMPLIFY_HXX

#include "autocog/data/fta.hxx"

#include <string>

namespace autocog::runtime::fta {

/// How much of a fully-determined FTA to collapse.
enum class Simplification {
  None,     ///< Leave it alone.
  Limited,  ///< Merge runs of text that belong to the same schema field, and
            ///< the structure between them, keeping the boundaries.
  Full,     ///< One text action: the flat token list.
};

Simplification parse_simplification(std::string const & name);
char const * simplification_name(Simplification mode);

/// Collapse a fully-determined FTA into fewer, larger text actions.
///
/// "Fully determined" means one chain of text actions with nothing left to
/// decide, which is what instantiating a prompt with a complete frame
/// produces. A choose or a completion anywhere means the frame did not cover
/// the prompt, and that is an error rather than something to approximate.
///
/// This exists for finetuning. Training wants the rendered prompt as the model
/// would read it — a flat sequence (Full), or a short list of segments each
/// tagged with the schema field it came from (Limited), so a loss can weight
/// the generated values differently from the structure carrying them.
///
/// Limited's segmentation is the alternation the renderer already produces:
/// every action that carries a field index is a value, everything between them
/// (labels, descriptions, separators) is structure. Each output action keeps
/// the uid of the first action in its group, which encodes the STA path.
///
/// The result is NOT interchangeable with its input. Tokenizing "A" then "B"
/// is not tokenizing "AB" — merging text moves the token boundaries at every
/// seam — so a simplified FTA yields a different token sequence from the one
/// the runtime actually evaluates. That is the point for finetuning, where the
/// natural tokenization of the whole text is what a language model sees, and
/// it is fatal for friction measurement, which is about the tokens the runtime
/// did force. The result is stamped with SIMPLIFY_PROVENANCE and the evaluator
/// refuses to score it, so the two cannot be silently mixed.
data::FTA simplify(data::FTA const & fta, Simplification mode);

}

#endif // AUTOCOG_RUNTIME_FTA_SIMPLIFY_HXX
