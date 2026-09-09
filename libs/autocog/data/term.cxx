#include "autocog/data/term.hxx"

#include "autocog/data/utility.hxx"

namespace autocog::data {

std::string TermExpr::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(kind));
  h.put(scalar);
  h.put(value);
  h.put(static_cast<unsigned>(operands.size()));
  for (auto const & op : operands) h.put(op.hash());
  return h.hash();
}

}
