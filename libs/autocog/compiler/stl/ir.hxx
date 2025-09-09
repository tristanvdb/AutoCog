#ifndef AUTOCOG_COMPILER_STL_IR_HXX
#define AUTOCOG_COMPILER_STL_IR_HXX

#include <unordered_map>
#include <string>
#include <variant>

namespace autocog::compiler::stl::ir {

using Value = std::variant<int, float, bool, std::string>;

struct Record {
  // TODO
};

struct Prompt {
  // TODO
};

using VarMap = std::unordered_map<std::string, ir::Value>;

}

#endif /* AUTOCOG_COMPILER_STL_IR_HXX */
