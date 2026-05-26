#ifndef AUTOCOG_COMPILER_STL_INSTANCE_SCANNER_HXX
#define AUTOCOG_COMPILER_STL_INSTANCE_SCANNER_HXX

#include "autocog/compiler/stl/ast.hxx"

#include <vector>

namespace autocog::compiler::stl {

class Driver;

class InstanceScanner {
  public:
    using NodePath = std::vector<ast::NodePtr>;

  private:
    Driver & driver;
    NodePath path;
    std::unordered_map<ast::ObjectRefPtr, NodePath> objects;
    std::unordered_map<ast::FormatRefPtr, NodePath> formats;

  public:
    InstanceScanner(Driver &);

    template <ast::Tag tagT>
    bool shortcut(ast::Node<tagT> const &) const {
      return false;
    }

    template <ast::Tag tagT>
    void pre(ast::Node<tagT> const & node) {
      path.push_back(&node);
    }

    template <ast::Tag tagT>
    void post(ast::Node<tagT> const &) {
      path.pop_back();
    }

    friend Driver;
};

template <>
void InstanceScanner::pre<ast::Tag::ObjectRef>(ast::ObjectRef const &);

template <>
void InstanceScanner::pre<ast::Tag::FormatRef>(ast::FormatRef const &);


}

#endif // AUTOCOG_COMPILER_STL_INSTANCE_SCANNER_HXX
