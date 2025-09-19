#ifndef AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX
#define AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX

namespace autocog::compiler::stl::ast {

DATA(Bind) {
  NODE(Path) source;
  ONODE(Path) target; // if missing then '_'
};
TRAVERSE_CHILDREN(Bind, source, target)

DATA(Ravel) {
  ONODE(Expression) depth; // if missing then 1
  ONODE(Path) target; // if missing then '_'
};
TRAVERSE_CHILDREN(Ravel, depth, target)

DATA(Wrap) {
  ONODE(Path) target; // if missing then '_'
};
TRAVERSE_CHILDREN(Wrap, target)

DATA(Prune) {
  NODE(Path) target;
};
TRAVERSE_CHILDREN(Prune, target)

DATA(Mapped) {
  ONODE(Path) target; // if missing then '_'
};
TRAVERSE_CHILDREN(Mapped, target)

DATA(Kwarg) {
  NODE(Identifier) name;
  VARIANT(FieldRef, Path, Expression) source; // "use" (a field), "get" (an input), "is" (a compile-time value),
  VARIANTS(Bind, Ravel, Wrap, Prune, Mapped) clauses;
};
TRAVERSE_CHILDREN(Kwarg, name, source, clauses)

DATA(Call) {
  NODE(ObjectRef) entry; //< Could resolve to a python symbol
  NODES(Kwarg) arguments;
};
TRAVERSE_CHILDREN(Call, entry, arguments)

DATA(Link) {
  NODE(Path) target;
  VARIANT(FieldRef, Path, Expression, Call) source; // "use" (a field), "get" (an input), "is" (a compile-time value), or "call" (a callable)
  VARIANTS(Bind, Ravel, Prune, Wrap) clauses;
};
TRAVERSE_CHILDREN(Link, target, source, clauses)

DATA(Channel) {
  NODES(Link) links;
};
TRAVERSE_CHILDREN(Channel, links)

}

#endif // AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX
