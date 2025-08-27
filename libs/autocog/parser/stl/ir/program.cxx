

#include "autocog/parser/stl/ir.hxx"
#include <stdexcept>

namespace autocog::parser {

using IrExecProgram = IrExec<IrTag::Program>;
using IrBaseProgram = BaseExec<IrTag::Program>;

void IrExecProgram::queue_imports(std::queue<std::string> & queue) const {
    for (auto & import: node.data.imports) {
      queue.push(import.data.file.data.value);
    }
}

template <>
void IrBaseProgram::semcheck(Parser const & parser, std::string filename, std::list<Diagnostic> & diagnostics) const {
    throw std::runtime_error("Not implemented yet : IrBaseProgram::semcheck()");
}

}
