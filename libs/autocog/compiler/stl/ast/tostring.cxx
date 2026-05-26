
#include "autocog/compiler/stl/ast/tostring.hxx"

namespace autocog::compiler::stl::ast {

std::string toString(std::monostate const &) {
    return "null";
}

std::string toString(Identifier const & node) {
    return node.data.name;
}

std::string toString(Integer const & node) {
    return std::to_string(node.data.value);
}

std::string toString(Float const & node) {
    return std::to_string(node.data.value);
}

std::string toString(Boolean const & node) {
    return node.data.value ? "true" : "false";
}

std::string toString(String const & node) {
    std::stringstream ss;
    if (node.data.is_format) {
        ss << "f\"" << node.data.value << "\"";
    } else {
        ss << "\"" << node.data.value << "\"";
    }
    return ss.str();
}

std::string toString(Unary const & node) {
    std::stringstream ss;
    ss << "(" << opKindToString(node.data.kind) << " " << toString_generic(node.data.operand) << ")";
    return ss.str();
}

std::string toString(Binary const & node) {
    std::stringstream ss;
    ss << "(" << toString_generic(node.data.lhs) << " " << opKindToString(node.data.kind) << " " << toString_generic(node.data.rhs) << ")";
    return ss.str();
}

std::string toString(Conditional const & node) {
    std::stringstream ss;
    ss << "(" << toString_generic(node.data.cond) << " ? " 
       << toString_generic(node.data.e_true) << " : " 
       << toString_generic(node.data.e_false) << ")";
    return ss.str();
}

std::string toString(Parenthesis const & node) {
    std::stringstream ss;
    ss << "(" << toString_generic(node.data.expr) << ")";
    return ss.str();
}

std::string toString(Expression const & node) {
    return toString_generic(node.data.expr);
}

std::string toString(Assign const & node) {
    std::stringstream ss;
    ss << toString(node.data.argument) << "=" << toString(node.data.value);
    return ss.str();
}

std::string toString(ObjectRef const & node) {
    std::stringstream ss;
    ss << "ObjectRef{" << toString(node.data.name);
    if (!node.data.config.empty()) {
        ss << ", config=" << toString_generic(node.data.config);
    }
    ss << "}";
    return ss.str();
}

std::string toString(Text const &) {
    return "Text{}";
}

std::string toString(Enum const & node) {
    std::stringstream ss;
    ss << "Enum" << toString_generic(node.data.enumerators);
    return ss.str();
}

std::string toString(Choice const & node) {
    std::stringstream ss;
    ss << "Choice{";
    ss << (node.data.mode == ChoiceKind::Repeat ? "Repeat" : "Select");
    ss << ", " << toString_generic(node.data.source) << "}";
    return ss.str();
}

std::string toString(FormatRef const & node) {
    std::stringstream ss;
    ss << "FormatRef{type=" << toString_generic(node.data.type);
    if (!node.data.args.empty()) {
        ss << ", args=" << toString_generic(node.data.args);
    }
    if (!node.data.kwargs.empty()) {
        ss << ", kwargs=" << toString_generic(node.data.kwargs);
    }
    ss << "}";
    return ss.str();
}


std::string toString(Path const &) {
    return "Path{...}";
}

// refString implementations for ObjectRef and FormatRef
// These provide a more concise, reference-like syntax for debugging

std::string refString(ObjectRef const & node) {
    std::stringstream ss;
    ss << toString(node.data.name);
    
    if (!node.data.config.empty()) {
        ss << "(";
        bool first = true;
        for (auto const & assign : node.data.config) {
            if (!first) ss << ", ";
            ss << toString(assign.data.argument) << "=" << toString(assign.data.value);
            first = false;
        }
        ss << ")";
    }
    
    return ss.str();
}

std::string refString(FormatRef const & node) {
    std::stringstream ss;
    
    // Type name
    std::visit([&ss](auto const & type) {
        if constexpr (std::is_same_v<std::decay_t<decltype(type)>, Identifier>) {
            ss << type.data.name;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(type)>, Text>) {
            ss << "Text";
        } else if constexpr (std::is_same_v<std::decay_t<decltype(type)>, Enum>) {
            ss << "Enum";
            if (!type.data.enumerators.empty()) {
                ss << "{";
                bool first = true;
                for (auto const & e : type.data.enumerators) {
                    if (!first) ss << "|";
                    ss << e.data.value;
                    first = false;
                }
                ss << "}";
            }
        } else if constexpr (std::is_same_v<std::decay_t<decltype(type)>, Choice>) {
            ss << "Choice<" << (type.data.mode == ChoiceKind::Repeat ? "Repeat" : "Select") << ">";
        }
    }, node.data.type);
    
    // Arguments and keyword arguments
    if (!node.data.args.empty() || !node.data.kwargs.empty()) {
        ss << "(";
        bool first = true;
        
        // Positional arguments
        for (auto const & arg : node.data.args) {
            if (!first) ss << ", ";
            ss << toString(arg);
            first = false;
        }
        
        // Keyword arguments
        for (auto const & kwarg : node.data.kwargs) {
            if (!first) ss << ", ";
            ss << toString(kwarg.data.argument) << "=" << toString(kwarg.data.value);
            first = false;
        }
        
        ss << ")";
    }
    
    return ss.str();
}

}

