#ifndef AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
#define AUTOCOG_COMPILER_STL_AST_DEFINE_HXX

namespace autocog::compiler::stl::ast {

// A named symbol bound to an expression. `Define` and `Argument` bodies are
// value expressions (evaluated by the Evaluator); `Vocab` bodies are vocab
// expressions (translated by the dedicated vocab mechanism, never value-
// evaluated). The kind is set from the keyword at parse time, so no type
// resolution is needed to tell them apart. `init` is optional only for
// Argument (a parameter may have no default); Define and Vocab require it.
enum class DefineKind { Define, Argument, Vocab };

DATA(Define) {
  DefineKind kind;
  NODE(Identifier) name;
  ONODE(Expression) init;
};
TRAVERSE_CHILDREN(Define, name, init)

}

#endif // AUTOCOG_COMPILER_STL_AST_DEFINE_HXX
