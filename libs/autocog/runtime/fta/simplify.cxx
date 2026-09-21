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

/// Index of every action by uid, and how many actions name each as successor.
struct Graph {
  std::map<std::string, std::size_t> by_uid;
  std::vector<unsigned> preds;
  std::vector<std::size_t> sole_pred;   ///< Valid where preds == 1.

  explicit Graph(FTA const & fta)
    : preds(fta.actions.size(), 0), sole_pred(fta.actions.size(), 0) {
    for (std::size_t i = 0; i < fta.actions.size(); ++i)
      by_uid.emplace(fta.actions[i].uid, i);
    for (std::size_t p = 0; p < fta.actions.size(); ++p)
      for (auto const & s : fta.actions[p].successors) {
        auto it = by_uid.find(s);
        if (it == by_uid.end())
          throw autocog::ConfigError(
              "Cannot simplify: action '" + fta.actions[p].uid
              + "' names a successor '" + s + "' that is not in the FTA.", s);
        preds[it->second] += 1;
        sole_pred[it->second] = p;
      }
  }

  std::size_t index(std::string const & uid) const { return by_uid.at(uid); }
};

bool is_text(Action const & a) {
  return std::holds_alternative<TextAction>(a.body);
}

/// Same schema field at the same position -- what `limited` keeps apart.
bool same_identity(Action const & a, Action const & b) {
  return a.field == b.field && a.indices == b.indices;
}

/// Can `b` be absorbed into the run ending at `a`? This is basic-block
/// formation: the two must be text, `a` must go nowhere else, and nothing
/// else may enter `b`, or merging would lose an edge.
bool mergeable(Action const & a, Action const & b, unsigned b_preds,
               Simplification mode) {
  if (!is_text(a) || !is_text(b)) return false;
  // Defensive, and NOT covered by the tests: the instantiator never emits a
  // text action with two successors (branching is always a choose), so no
  // fixture reaches this. It stays because simplify() takes any data::FTA,
  // including one loaded from JSON, and merging a branching node would drop
  // every successor but the first.
  if (a.successors.size() != 1) return false;
  if (b_preds != 1) return false;
  if (mode == Simplification::Limited && !same_identity(a, b)) return false;
  return true;
}

}

FTA simplify(FTA const & source, Simplification mode) {
  if (mode == Simplification::None) return source;

  Graph const g(source);

  // Group the actions into maximal runs. A run starts at an action its
  // predecessor cannot absorb, and extends forward while the next one can be.
  // Iterating ascending keeps the order deterministic and puts the entry's
  // run first: actions[0] has no predecessor, so it always starts a run.
  std::vector<int> group_of(source.actions.size(), -1);
  std::vector<std::vector<std::size_t>> groups;

  for (std::size_t i = 0; i < source.actions.size(); ++i) {
    if (group_of[i] >= 0) continue;
    // Not a run head if the action upstream will absorb this one: leave it
    // for that run, which may be started later in this same loop.
    if (g.preds[i] == 1
        && mergeable(source.actions[g.sole_pred[i]], source.actions[i], 1, mode))
      continue;
    std::vector<std::size_t> run{i};
    group_of[i] = static_cast<int>(groups.size());
    for (;;) {
      Action const & last = source.actions[run.back()];
      if (last.successors.size() != 1) break;
      std::size_t const next = g.index(last.successors.front());
      if (group_of[next] >= 0) break;
      if (!mergeable(last, source.actions[next], g.preds[next], mode)) break;
      group_of[next] = static_cast<int>(groups.size());
      run.push_back(next);
    }
    groups.push_back(std::move(run));
  }

  // Anything left unassigned was skipped above because a predecessor claimed
  // it, but that predecessor's run stopped early (it had already been
  // assigned). Give each its own run so no action is dropped.
  for (std::size_t i = 0; i < source.actions.size(); ++i) {
    if (group_of[i] >= 0) continue;
    group_of[i] = static_cast<int>(groups.size());
    groups.push_back({i});
  }

  FTA out;
  out.actions.reserve(groups.size());
  for (auto const & run : groups) {
    Action const & head = source.actions[run.front()];
    Action action;
    // The first uid in the run: it encodes the STA path, which is the
    // provenance a weighted finetuning loss keys on.
    action.uid = head.uid;

    if (run.size() == 1) {
      action.body = head.body;          // a decision, or a lone text action
    } else {
      TextAction text;
      for (std::size_t idx : run) {
        auto const & body = std::get<TextAction>(source.actions[idx].body);
        text.text += body.text;
        text.evaluate = text.evaluate || body.evaluate;
      }
      action.body = std::move(text);
    }

    // Keep the schema identity when every action in the run agrees on it.
    // Under `limited` that is guaranteed; under `full` it happens whenever a
    // run did not cross a field boundary, and there is no reason to discard
    // provenance that is still unambiguous.
    bool uniform = true;
    for (std::size_t idx : run)
      uniform = uniform && same_identity(source.actions[idx], head);
    if (uniform) {
      action.field = head.field;
      action.indices = head.indices;
    }
    out.actions.push_back(std::move(action));
  }

  // Successors: whatever the run's last action pointed at, remapped to the
  // run that now contains it. Index alignment with a choose's candidate list
  // is preserved because a choose is never merged into anything.
  for (std::size_t gi = 0; gi < groups.size(); ++gi) {
    Action const & tail = source.actions[groups[gi].back()];
    for (auto const & s : tail.successors)
      out.actions[gi].successors.push_back(
          out.actions[group_of[g.index(s)]].uid);
  }

  out.queue_metric = source.queue_metric;
  out.queue_stop   = source.queue_stop;
  // Vocabs mask completions. Carry them only if a completion survived.
  for (auto const & a : out.actions)
    if (std::holds_alternative<autocog::data::CompleteAction>(a.body)) {
      out.vocabs = source.vocabs;
      break;
    }

  // Provenance is parents-by-hash: the source FTA is a parent, the mode is
  // not -- it is a property of these actions, so it gets its own field.
  out.provenance = source.provenance;
  out.provenance["fta"] = source.metadata ? source.metadata->hash : std::string{};
  out.simplified = simplification_name(mode);
  out.finalize();
  return out;
}

}
