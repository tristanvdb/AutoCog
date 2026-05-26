
#include "convert.hxx"

#include "autocog/compiler/stl/parser.hxx"
#include "autocog/compiler/stl/ast.hxx"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

PYBIND11_MODULE(stl_cxx, module) {
  module.doc() = "C++ STL parser for AutoCog";

  // TODO
}

