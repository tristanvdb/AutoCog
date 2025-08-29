

#include "autocog/parser/stl/ir.hxx"
#include <stdexcept>

namespace autocog::parser {

using IrExecProgram = IrExec<IrTag::Program>;
using IrBaseProgram = BaseExec<IrTag::Program>;

void IrExecProgram::queue_imports(std::queue<std::string> & queue) const {
  for (auto & import: node.data.imports) {
    std::string const & file = import.data.file;
    bool has_stl_extension = file.size() >= 4 && ( file.rfind(".stl") == file.size() - 4 );
    if (has_stl_extension) queue.push(file);
  }
}

template <>
void IrBaseProgram::semcheck(Parser const & parser, std::string filename, std::list<Diagnostic> & diagnostics) const {
    throw std::runtime_error("Not implemented yet : IrBaseProgram::semcheck()");
}

}
