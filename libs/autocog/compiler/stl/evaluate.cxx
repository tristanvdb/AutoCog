
#include "evaluate.hxx"

#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

Evaluator::Evaluator(std::list<Diagnostic> & diagnostics_) :
  diagnostics(diagnostics_)
{}

} // namespace autocog::compiler

