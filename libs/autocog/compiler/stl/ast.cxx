
#include "autocog/compiler/stl/ast.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl::ast {

const std::unordered_map<std::string, Tag> tags = {
    {"Program", Tag::Program},
    {"Import", Tag::Import},
    {"Export", Tag::Export},
    {"Enum", Tag::Enum},
    {"Choice", Tag::Choice},
    {"Annotate", Tag::Annotate},
    {"Annotation", Tag::Annotation},
    {"Define", Tag::Define},
    {"Flow", Tag::Flow},
    {"Edge", Tag::Edge},
    {"Struct", Tag::Struct},
    {"Field", Tag::Field},
    {"Search", Tag::Search},
    {"Param", Tag::Param},
    {"Retfield", Tag::Retfield},
    {"Return", Tag::Return},
    {"Record", Tag::Record},
    {"Format", Tag::Format},
    {"Text", Tag::Text},
    {"Prompt", Tag::Prompt},
    {"FieldRef", Tag::FieldRef},
    {"PromptRef", Tag::PromptRef},
    {"Channel", Tag::Channel},
    {"Link", Tag::Link},
    {"Bind", Tag::Bind},
    {"Ravel", Tag::Ravel},
    {"Mapped", Tag::Mapped},
    {"Prune", Tag::Prune},
    {"Wrap", Tag::Wrap},
    {"Call", Tag::Call},
    {"Kwarg", Tag::Kwarg},
    {"Path", Tag::Path},
    {"Step", Tag::Step},
    {"Expression", Tag::Expression},
    {"Identifier", Tag::Identifier},
    {"Integer", Tag::Integer},
    {"Float", Tag::Float},
    {"Boolean", Tag::Boolean},
    {"String", Tag::String},
    {"Unary", Tag::Unary},
    {"Binary", Tag::Binary},
    {"Conditional", Tag::Conditional},
    {"Parenthesis", Tag::Parenthesis}
};

}

