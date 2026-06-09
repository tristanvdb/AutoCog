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
/// @param ftt          The FTT tree (root node) as produced by the backend.
/// @param sta          The STA program (provides each prompt's field table).
/// @param prompt_name  The prompt whose fields the FTT corresponds to.
/// @param content      The input content (a Document); used to resolve `select`
///                     choices to their actual values. Pass a null Document to
///                     skip.
/// @param resolve_selects  When true (and content is available), a `select`
///                     choice field's index value is resolved to the actual
///                     value from `content`; otherwise the raw index is kept.
/// @param metric       Path-selection metric. "best" picks the completed path
///                     whose leaf has the highest score (matching FTT::proba()).
///                     Unknown values raise ConfigError.
/// @return The frame: a nested Document of field name -> value.
autocog::types::Document walk_ftt_to_frame(
    autocog::data::FTT const & ftt,
    autocog::data::STA const & sta,
    std::string const & prompt_name,
    autocog::types::Document const & content,
    bool resolve_selects,
    std::string const & metric = "best"
);

} // namespace autocog::runtime::sta

#endif /* AUTOCOG_RUNTIME_STA_WALK_HXX */
