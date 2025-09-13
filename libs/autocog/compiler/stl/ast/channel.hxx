#ifndef AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX
#define AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX

namespace autocog::compiler::stl::ast {

DATA(Bind) {
  NODE(Path) source;
  ONODE(Path) target; // if missing then '_'
};

DATA(Ravel) {
  ONODE(Expression) depth; // if missing then 1
  ONODE(Path) target; // if missing then '_'
};

DATA(Wrap) {
  ONODE(Path) target; // if missing then '_'
};

DATA(Prune) {
  NODE(Path) target;
};

DATA(Mapped) {
  ONODE(Path) target; // if missing then '_'
};

DATA(Kwarg) {
  NODE(Identifier) name;
  VARIANT(FieldRef, Path, Expression) source; // "use" (a field), "get" (an input), "is" (a compile-time value),
  VARIANTS(Bind, Ravel, Wrap, Prune, Mapped) clauses;
};

DATA(Call) {
  NODE(PromptRef) entry; //< Could resolve to a python symbol but use PromptRef as there is no difference at parse time
  NODES(Kwarg) arguments;
};

DATA(Link) {
  NODE(Path) target;
  VARIANT(FieldRef, Path, Expression, Call) source; // "use" (a field), "get" (an input), "is" (a compile-time value), or "call" (a callable)
  VARIANTS(Bind, Ravel, Prune, Wrap) clauses;
};

DATA(Channel) {
  NODES(Link) links;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX
