
#include "autocog/llama/ftt.hxx"

namespace autocog { namespace llama {

pybind11::dict FTT::pydict() const {
  throw std::runtime_error("Exporting FTT to python dictionary is not implemented yet!");
}

} }

