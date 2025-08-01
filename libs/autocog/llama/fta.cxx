
#include "autocog/llama/fta.hxx"

namespace autocog { namespace llama {

Action::Action(ActionKind const kind_, NodeID const id_) : kind(kind_), id(id_) {}

Text::Text(NodeID const id_) : Action(ActionKind::Text, id_) {}

Completion::Completion(NodeID const id_) : Action(ActionKind::Completion, id_) {}

Choice::Choice(NodeID const id_) : Action(ActionKind::Choice, id_) {}


} }

