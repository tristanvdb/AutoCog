
#include "instantiate.hxx"

#include <stdexcept>

namespace autocog::compiler::stl {

Instantiator::Instantiator(std::list<Diagnostic> & diagnostics_) :
  diagnostics(diagnostics_),
  programs(),
  declaration_to_file(),
  records(),
  prompts(),
  instances(),
  record_cache(),
  instantiations()
{}

void Instantiator::defines(ast::Program const & program) {
  throw std::runtime_error("NIY: Instantiator::defines");
}

void Instantiator::declarations(ast::Program const & program) {
  throw std::runtime_error("NIY: Instantiator::declarations");
}

void Instantiator::entries(ast::Program const & program) {
  throw std::runtime_error("NIY: Instantiator::entries");
}

void Instantiator::collect() {
  throw std::runtime_error("NIY: Instantiator::collect");
}

void Instantiator::instantiate() {
  throw std::runtime_error("NIY: Instantiator::instantiate");
}

} // namespace autocog::compiler

