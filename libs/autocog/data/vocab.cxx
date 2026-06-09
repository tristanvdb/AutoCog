#include "autocog/data/vocab.hxx"

#include "autocog/data/utility.hxx"

namespace autocog::data {

std::string VocabExpr::hash() const {
  ContentHasher h;
  h.put(static_cast<unsigned>(kind));
  h.put(strings);
  h.put(static_cast<unsigned>(operands.size()));
  for (auto const & op : operands) h.put(op.hash());
  return h.hash();
}

}
