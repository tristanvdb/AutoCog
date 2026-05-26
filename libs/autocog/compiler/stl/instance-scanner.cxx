
#include "autocog/compiler/stl/instance-scanner.hxx"
#include "autocog/compiler/stl/driver.hxx"

#if VERBOSE
#  include <iostream>
#endif

namespace autocog::compiler::stl {

InstanceScanner::InstanceScanner(
  Driver & driver_
) :
  driver(driver_),
  path(),
  objects(),
  formats()
{}

template <>
void InstanceScanner::pre<ast::Tag::ObjectRef>(ast::ObjectRef const & node) {
  this->objects.emplace(&node, this->path);
  path.push_back(&node);
}

template <>
void InstanceScanner::pre<ast::Tag::FormatRef>(ast::FormatRef const & node) {
  this->formats.emplace(&node, this->path);
  path.push_back(&node);
}

}

