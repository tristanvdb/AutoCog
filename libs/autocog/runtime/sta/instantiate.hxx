#ifndef AUTOCOG_RUNTIME_STA_INSTANTIATE_HXX
#define AUTOCOG_RUNTIME_STA_INSTANTIATE_HXX

#include "autocog/runtime/sta/state.hxx"
#include "autocog/data/syntax.hxx"
#include "autocog/data/search.hxx"
#include "autocog/data/fta.hxx"
#include "autocog/utilities/types.hxx"

namespace autocog::runtime::sta {

// Instantiate a prompt into a text-level FTA.
// content: pre-resolved channel data as a Document (object at the top level).
// search:  search config (beams, width, etc.) embedded into FTA actions.
// sta_uid: content uid of the source STA (this FTA's provenance parent); the
//          caller holds the program/STA and supplies it.
// Returns: a finalized autocog::data::FTA.
autocog::data::FTA instantiate(
    autocog::data::Prompt const & prompt,
    autocog::types::Document const & content,
    autocog::data::Syntax const & syntax,
    autocog::data::SearchConfig const & search,
    std::string const & sta_uid
);

}

#endif // AUTOCOG_RUNTIME_STA_INSTANTIATE_HXX
