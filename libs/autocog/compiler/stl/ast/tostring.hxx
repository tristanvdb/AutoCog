#ifndef AUTOCOG_COMPILER_STL_AST_TOSTRING_HXX
#define AUTOCOG_COMPILER_STL_AST_TOSTRING_HXX

#include "autocog/compiler/stl/ast.hxx"

#include <string>
#include <sstream>
#include <type_traits>

namespace autocog::compiler::stl::ast {

std::string toString(std::monostate const &);
std::string toString(Identifier const &);
std::string toString(Integer const &);
std::string toString(Float const &);
std::string toString(Boolean const &);
std::string toString(String const &);
std::string toString(Unary const &);
std::string toString(Binary const &);
std::string toString(Conditional const &);
std::string toString(Parenthesis const &);
std::string toString(Expression const &);
std::string toString(Assign const &);
std::string toString(ObjectRef const &);
std::string toString(Text const &);
std::string toString(Enum const &);
std::string toString(Choice const &);
std::string toString(FormatRef const &);
std::string toString(Path const &);

std::string refString(ObjectRef const &);
std::string refString(FormatRef const &);

template<typename T>
std::enable_if_t<is_any_node_container_v<T>, std::string> toString_generic(T const & container) {
    if constexpr (is_node_v<T>) {
        return toString(container);
    } else if constexpr (is_pnode_v<T>) {
        if (container) return toString(*container);
        return "null";
    } else if constexpr (is_onode_v<T>) {
        if (container) return toString(*container);
        return "null";
    } else if constexpr (is_nodes_v<T>) {
        std::stringstream ss;
        ss << "[";
        bool first = true;
        for (auto const & node : container) {
            if (!first) ss << ", ";
            ss << toString(node);
            first = false;
        }
        ss << "]";
        return ss.str();
    } else if constexpr (is_pnodes_v<T>) {
        std::stringstream ss;
        ss << "[";
        bool first = true;
        for (auto const & pnode : container) {
            if (!first) ss << ", ";
            if (pnode) ss << toString(*pnode);
            else ss << "null";
            first = false;
        }
        ss << "]";
        return ss.str();
    } else if constexpr (is_variant_v<T>) {
        return std::visit([](auto const & node) {
            return toString(node);
        }, container);
    } else if constexpr (is_variants_v<T>) {
        std::stringstream ss;
        ss << "[";
        bool first = true;
        for (auto const & vnode : container) {
            if (!first) ss << ", ";
            ss << std::visit([](auto const & node) {
                return toString(node);
            }, vnode);
            first = false;
        }
        ss << "]";
        return ss.str();
    }
}

}

#endif /* AUTOCOG_COMPILER_STL_AST_TOSTRING_HXX */

