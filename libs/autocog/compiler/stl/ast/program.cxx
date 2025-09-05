
#include "autocog/compiler/stl/ast.hxx"
#include <stdexcept>

namespace autocog::compiler::stl {

using AstExecProgram = AstExec<AstTag::Program>;
using AstBaseProgram = BaseExec<AstTag::Program>;

void AstExecProgram::queue_imports(std::queue<std::string> & queue) const {
  for (auto & import: node.data.imports) {
    std::string const & file = import.data.file;
    bool has_stl_extension = file.size() >= 4 && ( file.rfind(".stl") == file.size() - 4 );
    if (has_stl_extension) queue.push(file);
  }
}

template <>
void AstBaseProgram::semcheck(Parser const & parser, std::string filename, std::list<Diagnostic> & diagnostics) const {
    throw std::runtime_error("Not implemented yet : IrBaseProgram::semcheck()");
}

}

