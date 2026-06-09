#include "autocog/data/ftt.hxx"

#include "autocog/data/utility.hxx"

#include <algorithm>

namespace autocog::data {

std::string FTTNode::hash() const {
  ContentHasher h;
  h.put(action)
   .put(uid)
   .put(field)
   .put(indices)
   .put(logprob)
   .put(logprobs)
   .put(length)
   .put(pruned)
   .put(tokens);
  h.put(static_cast<unsigned>(children.size()));
  // Child order reflects an exploration heuristic (possibly parallel) and is not
  // enforced by the structure, so fold children in a canonical order. Each
  // child's hash already covers its whole subtree, so sorting on it alone makes
  // the result independent of sibling order.
  std::vector<std::string> kids;
  kids.reserve(children.size());
  for (auto const & c : children) kids.push_back(c.hash());
  std::sort(kids.begin(), kids.end());
  for (auto const & k : kids) h.put(k);
  return h.hash();
}

std::string FTT::content_hash() const {
  return root.hash();
}

}
