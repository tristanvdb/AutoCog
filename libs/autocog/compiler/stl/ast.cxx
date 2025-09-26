
#include "autocog/compiler/stl/ast.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl::ast {

const std::unordered_map<std::string, Tag> tags = {
#define X(etag,stag) {stag,Tag::etag},
#include "autocog/compiler/stl/ast/nodes.def"
};
const std::unordered_map<Tag, std::string> rtags = {
#define X(etag,stag) {Tag::etag,stag},
#include "autocog/compiler/stl/ast/nodes.def"
};

}

