
#include "autocog/compiler/stl/ast.hxx"
#include <stdexcept>

namespace autocog::compiler::stl::ast {

void Exec<Tag::Program>::queue_imports(std::queue<std::string> & queue) const {
  for (auto & import: node.data.imports) {
    std::string const & file = import.data.file;
    bool has_stl_extension = file.size() >= 4 && ( file.rfind(".stl") == file.size() - 4 );
    if (has_stl_extension) queue.push(file);
  }
}

}

