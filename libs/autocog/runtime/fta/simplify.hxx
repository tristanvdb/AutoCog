#ifndef AUTOCOG_RUNTIME_FTA_SIMPLIFY_HXX
#define AUTOCOG_RUNTIME_FTA_SIMPLIFY_HXX

#include "autocog/data/fta.hxx"

#include <string>

namespace autocog::runtime::fta {

/// How far to collapse the fixed text of an FTA.
enum class Simplification {
  None,     ///< Leave it alone.
  Limited,  ///< Merge adjacent text belonging to the same schema field, so
            ///< every value keeps its own segment and the structure between
            ///< values collapses into one.
  Full,     ///< Merge adjacent text regardless of which field it came from.
};

Simplification parse_simplification(std::string const & name);
char const * simplification_name(Simplification mode);

/// Collapse runs of adjacent text actions, leaving decisions as boundaries.
///
/// The merge rule is basic-block formation: two text actions join when the
/// first goes nowhere else and nothing else enters the second, so no edge is
/// ever lost. A choose or a completion is never merged into anything — it ends
/// the run before it and starts the one after — which is what makes this safe
/// to apply to any FTA rather than only to a forced path.
///
/// On an FTA instantiated from a COMPLETE frame there are no decisions left,
/// so Full yields exactly one action: the flat token list. That is the
/// finetuning case, and the single action is a consequence of the input being
/// determined, not a preconditon this imposes. On a partly determined FTA the
/// result is the same automaton with its fixed text gathered up, which is what
/// makes it readable — the mcq demos go from 25 actions to 5.
///
/// Limited's segmentation is the alternation the renderer already produces:
/// an action carrying a field index is a value, everything between them
/// (labels, descriptions, separators) is structure. A weighted finetuning loss
/// keys on that split. Each output action keeps the uid of the first action in
/// its run, which encodes the STA path, and keeps the field identity whenever
/// the whole run agrees on one.
///
/// The result is NOT interchangeable with its input. Tokenizing "A" then "B"
/// is not tokenizing "AB" — merging text moves the token boundaries at every
/// seam — so a simplified FTA yields a different token sequence from the one
/// the runtime actually evaluates. That is the point for finetuning, where the
/// natural tokenization of the whole text is what a language model sees, and
/// it is fatal for friction measurement, which is about the tokens the runtime
/// did force. The result carries data::FTA::simplified and the evaluator
/// refuses to score it, so the two cannot be silently mixed.
data::FTA simplify(data::FTA const & fta, Simplification mode);

}

#endif // AUTOCOG_RUNTIME_FTA_SIMPLIFY_HXX
