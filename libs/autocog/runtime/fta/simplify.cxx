#include "autocog/runtime/fta/simplify.hxx"

#include "autocog/utilities/errors.hxx"

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace autocog::runtime::fta {

using autocog::data::Action;
using autocog::data::FTA;
using autocog::data::TextAction;

Simplification parse_simplification(std::string const & name) {
  if (name == "none" || name.empty()) return Simplification::None;
  if (name == "limited")              return Simplification::Limited;
  if (name == "full")                 return Simplification::Full;
  throw autocog::ConfigError(
      "Unknown simplification '" + name + "' (expected: none, limited, full)",
      name);
}

char const * simplification_name(Simplification mode) {
  switch (mode) {
    case Simplification::Limited: return "limited";
    case Simplification::Full:    return "full";
    case Simplification::None:    break;
  }
  return "none";
}

namespace {

/// The forced chain, entry first. Throws if the FTA still has a decision in
/// it: the caller asked to flatten a path that does not exist yet.
std::vector<Action const *> forced_chain(FTA const & fta) {
  std::map<std::string, Action const *> by_uid;
  for (auto const & a : fta.actions) by_uid.emplace(a.uid, &a);

  std::vector<Action const *> chain;
  Action const * cur = fta.actions.empty() ? nullptr : &fta.actions.front();
  while (cur) {
    if (!std::holds_alternative<TextAction>(cur->body))
      throw autocog::ConfigError(
          "Cannot simplify: action '" + cur->uid + "' still generates. "
          "Simplification flattens a path the frame has already fixed — "
          "instantiate with a complete frame as content first.",
          cur->uid);
    if (cur->successors.size() > 1)
      throw autocog::ConfigError(
          "Cannot simplify: action '" + cur->uid + "' branches "
          + std::to_string(cur->successors.size()) + " ways.",
          cur->uid);
    chain.push_back(cur);
    if (cur->successors.empty()) break;
    auto it = by_uid.find(cur->successors.front());
    if (it == by_uid.end())
      throw autocog::ConfigError(
          "Cannot simplify: action '" + cur->uid + "' names a successor '"
          + cur->successors.front() + "' that is not in the FTA.",
          cur->successors.front());
    cur = it->second;
  }

  // Every action must be on the chain. One that is not is unreachable, and
  // dropping it silently would mean simplifying a different prompt than the
  // one that was asked for.
  if (chain.size() != fta.actions.size())
    throw autocog::ConfigError(
        "Cannot simplify: " + std::to_string(fta.actions.size() - chain.size())
        + " of " + std::to_string(fta.actions.size())
        + " actions are unreachable from the entry.", "actions");

  return chain;
}

/// Two adjacent actions belong to the same segment when they describe the same
/// thing: the same schema field at the same indices, or both structure.
bool same_segment(Action const & a, Action const & b) {
  return a.field == b.field && a.indices == b.indices;
}

}

FTA simplify(FTA const & source, Simplification mode) {
  if (mode == Simplification::None) return source;

  std::vector<Action const *> const chain = forced_chain(source);

  // Group the chain: Full is one group, Limited splits wherever the field
  // identity changes.
  std::vector<std::vector<Action const *>> groups;
  for (Action const * a : chain) {
    bool const start_new = groups.empty()
        || (mode == Simplification::Limited && !same_segment(*groups.back().back(), *a));
    if (start_new) groups.emplace_back();
    groups.back().push_back(a);
  }

  FTA out;
  out.actions.reserve(groups.size());
  for (std::size_t i = 0; i < groups.size(); ++i) {
    auto const & group = groups[i];
    TextAction text;
    for (Action const * a : group) {
      auto const & body = std::get<TextAction>(a->body);
      text.text += body.text;
      text.evaluate = text.evaluate || body.evaluate;
    }
    Action action;
    // The first uid in the group: it encodes the STA path, which is the
    // provenance a weighted finetuning loss needs to key on.
    action.uid  = group.front()->uid;
    action.body = std::move(text);
    if (mode == Simplification::Limited) {
      action.field   = group.front()->field;
      action.indices = group.front()->indices;
    }
    out.actions.push_back(std::move(action));
  }
  for (std::size_t i = 0; i + 1 < out.actions.size(); ++i)
    out.actions[i].successors.push_back(out.actions[i + 1].uid);

  out.queue_metric = source.queue_metric;
  out.queue_stop   = source.queue_stop;
  // Vocabs mask completions, and a forced chain has none: carrying the table
  // would be carrying a table nothing can reference.

  // Provenance is parents-by-hash: the source FTA is a parent, the mode is
  // not — it is a property of these actions, so it gets its own field.
  out.provenance = source.provenance;
  out.provenance["fta"] = source.metadata ? source.metadata->hash : std::string{};
  out.simplified = simplification_name(mode);
  out.finalize();
  return out;
}

}
