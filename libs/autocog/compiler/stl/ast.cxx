
#include "autocog/compiler/stl/ast.hxx"
#include "autocog/utilities/exception.hxx"

#include <sstream>
#include <stdexcept>


namespace autocog::compiler::stl::ast {

const std::unordered_map<std::string, Tag> tags = {
#define X(etag,stag) {stag,Tag::etag},
#include "autocog/compiler/stl/ast/nodes.def"
};
const std::unordered_map<Tag, std::string> rtags = {
#define X(etag,stag) {Tag::etag,stag},
#include "autocog/compiler/stl/ast/nodes.def"
};

std::string tag2str(Tag tag) {
  auto it = rtags.find(tag);
  if (it == rtags.end()) {
    std::ostringstream oss;
    oss << "Unknown ast::Tag for tag2str(" << (long)tag  << ")!";
    throw autocog::utilities::InternalError(oss.str());
  }
  return it->second;
}

Tag str2tag(std::string str) {
  auto it = tags.find(str);
  if (it == tags.end()) {
    std::ostringstream oss;
    oss << "Unrecognized ast::Tag for str2tag(" << str  << ")!";
    throw autocog::utilities::InternalError(oss.str());
  }
  return it->second;
}

}

