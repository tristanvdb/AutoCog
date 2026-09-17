#ifndef AUTOCOG_RUNTIME_STA_WALK_HXX
#define AUTOCOG_RUNTIME_STA_WALK_HXX

#include "autocog/runtime/sta/state.hxx"
#include "autocog/data/ftt.hxx"
#include "autocog/utilities/types.hxx"

#include <string>

namespace autocog::runtime::sta {

/// Walk an FTT and build the nested frame of field values for a prompt,
/// selecting a single root->leaf path according to `metric`.
///
/// This is the single canonical FTT->frame implementation.
///
/// A field's frame value is the text the model emitted at that field, verbatim.
/// Nothing is rewritten on the way out: a `select` field holds the INDEX that
/// was produced, not the value that index names. Resolving the index is the
/// program's job (`return call ...`), not the walker's — a frame that holds
/// "Water" where "1" was emitted describes no token stream, and forcing it back
/// would then require inverting a transformation the frame no longer records.
///
/// `content` seeds the frame. An FTT only records what its FTA made into
/// actions, so a simplified FTA — one whose fixed text has been gathered up —
/// carries no node for a field the content supplied, and those fields come
/// back absent. Seeding with the SAME document the prompt was instantiated
/// from fills exactly those, and the walk then overwrites wherever the FTT
/// actually recorded something.
///
/// Passing content is never wrong and never needed for an unsimplified FTT:
/// there, every field has a node and the walk overwrites the seed completely.
///
/// Seeded values are NOT rendered to text. The frame is then heterogeneous —
/// raw for seeded fields, emitted text for generated ones — deliberately:
/// converting here would bake in a transformation something downstream would
/// have to undo, and a value that reaches another instantiation is rendered
/// there anyway.
///
/// @param ftt          The FTT tree (root node) as produced by the backend.
/// @param sta          The STA program (provides each prompt's field table).
/// @param prompt_name  The prompt whose fields the FTT corresponds to.
/// @param metric       Path-selection metric. "best" picks the completed path
///                     whose leaf has the highest score (matching FTT::proba()).
///                     Unknown values raise ConfigError.
/// @param content      The channel-resolved content the prompt was
///                     instantiated with, or an empty document.
/// @return The frame: a nested Document of field name -> value.
autocog::types::Document walk_ftt_to_frame(
    autocog::data::FTT const & ftt,
    autocog::data::STA const & sta,
    std::string const & prompt_name,
    std::string const & metric = "best",
    autocog::types::Document const & content = autocog::types::Document{}
);

} // namespace autocog::runtime::sta

#endif /* AUTOCOG_RUNTIME_STA_WALK_HXX */
