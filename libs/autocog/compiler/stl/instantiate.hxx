#ifndef AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
#define AUTOCOG_COMPILER_STL_INSTANTIATE_HXX

#include "autocog/compiler/stl/ast.hxx"

#include "ir.pb.h"  // Generated protobuf header

#include <map>
#include <string>

namespace autocog::compiler::stl {

class Instantiator {
public:
    // Main entry point
    ir::Program instantiate(const ast::Program & ast);
};

}

#endif // AUTOCOG_COMPILER_STL_INSTANTIATE_HXX
