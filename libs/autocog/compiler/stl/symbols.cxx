
#include "autocog/compiler/stl/symbols.hxx"

#include <stdexcept>


namespace autocog::compiler::stl {

PythonSymbol::PythonSymbol(
  std::string const & filename_,
  std::string const & alias_,
  ast::ObjectRef const & target_
) :
  filename(filename_),
  alias(alias_),
  target(target_)
{}

}

