
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "autocog/compiler/stl/ast.hxx"
#include "helpers.hxx"

namespace autocog::compiler::stl {

using json = serialize::json;

namespace {

// ---- Expression tree ------------------------------------------------------

json expr_to_json(ast::Expression const & expr);

json string_to_json(ast::Node<ast::Tag::String> const & s) {
    return json{{"node", "String"}, {"value", s.data.value}, {"is_format", s.data.is_format}};
}

json expr_to_json(ast::Expression const & expr) {
    return std::visit([](auto const & node) -> json {
        using N = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<N, std::monostate>) {
            return nullptr;
        } else {
            constexpr ast::Tag tag = N::tag;
            auto const & d = node.data;
            if constexpr (tag == ast::Tag::Identifier) {
                return json{{"node", "Identifier"}, {"name", d.name}};
            } else if constexpr (tag == ast::Tag::Integer) {
                return json{{"node", "Integer"}, {"value", d.value}};
            } else if constexpr (tag == ast::Tag::Float) {
                return json{{"node", "Float"}, {"value", serialize::round3(d.value)}};
            } else if constexpr (tag == ast::Tag::Boolean) {
                return json{{"node", "Boolean"}, {"value", d.value}};
            } else if constexpr (tag == ast::Tag::String) {
                return json{{"node", "String"}, {"value", d.value}, {"is_format", d.is_format}};
            } else if constexpr (tag == ast::Tag::Unary) {
                return json{{"node", "Unary"}, {"op", ast::opKindToString(d.kind)},
                            {"operand", expr_to_json(*d.operand)}};
            } else if constexpr (tag == ast::Tag::Binary) {
                return json{{"node", "Binary"}, {"op", ast::opKindToString(d.kind)},
                            {"lhs", expr_to_json(*d.lhs)}, {"rhs", expr_to_json(*d.rhs)}};
            } else if constexpr (tag == ast::Tag::Conditional) {
                return json{{"node", "Conditional"}, {"cond", expr_to_json(*d.cond)},
                            {"true", expr_to_json(*d.e_true)}, {"false", expr_to_json(*d.e_false)}};
            } else if constexpr (tag == ast::Tag::Parenthesis) {
                return json{{"node", "Parenthesis"}, {"expr", expr_to_json(*d.expr)}};
            } else if constexpr (tag == ast::Tag::Tokenize) {
                json strs = json::array();
                for (auto const & s : d.strings) strs.push_back(string_to_json(s));
                return json{{"node", "Tokenize"}, {"strings", strs}};
            } else if constexpr (tag == ast::Tag::Regex) {
                return json{{"node", "Regex"}, {"pattern", string_to_json(d.pattern)}};
            } else {
                return json{{"node", "?"}};
            }
        }
    }, expr.data.expr);
}

char const * define_kind_str(ast::DefineKind k) {
    switch (k) {
        case ast::DefineKind::Define:   return "define";
        case ast::DefineKind::Argument: return "argument";
        case ast::DefineKind::Vocab:    return "vocab";
    }
    return "?";
}

json define_to_json(ast::Node<ast::Tag::Define> const & node) {
    json j;
    j["node"] = "Define";
    j["kind"] = define_kind_str(node.data.kind);
    j["name"] = node.data.name.data.name;
    if (node.data.init) j["init"] = expr_to_json(node.data.init.value());
    else                j["init"] = nullptr;
    return j;
}

// A statement-level variant (Program/Record/Prompt constructs). Only the kinds
// useful for inspection are expanded; others are tagged by name.
template <typename Variant>
json statement_to_json(Variant const & stmt) {
    return std::visit([](auto const & node) -> json {
        using N = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<N, std::monostate>) {
            return nullptr;
        } else {
            constexpr ast::Tag tag = N::tag;
            if constexpr (tag == ast::Tag::Define) {
                return define_to_json(node);
            } else if constexpr (tag == ast::Tag::Record) {
                json j{{"node", "Record"}, {"name", node.data.name.data.name}};
                json cs = json::array();
                for (auto const & c : node.data.constructs) cs.push_back(statement_to_json(c));
                j["constructs"] = cs;
                return j;
            } else if constexpr (tag == ast::Tag::Prompt) {
                json j{{"node", "Prompt"}, {"name", node.data.name.data.name}};
                json cs = json::array();
                for (auto const & c : node.data.constructs) cs.push_back(statement_to_json(c));
                j["constructs"] = cs;
                return j;
            } else {
                return json{{"node", ast::tag2str(tag)}};
            }
        }
    }, stmt);
}

} // namespace

void serialize_ast(Driver const & driver, std::ostream & out) {
    json output;
    output["files"] = json::array();
    for (auto const & program : driver.programs) {
        json file;
        file["fid"] = program.data.fid;
        file["filename"] = program.data.filename;
        json stmts = json::array();
        for (auto const & stmt : program.data.statements) {
            stmts.push_back(statement_to_json(stmt));
        }
        file["statements"] = stmts;
        output["files"].push_back(file);
    }
    out << output.dump(2) << std::endl;
}

}
