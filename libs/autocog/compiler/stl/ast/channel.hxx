#ifndef AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX
#define AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX

namespace autocog::compiler::stl::ast {

DATA(Kwarg) {
  NODE(Identifier) name;
  NODE(Path) source;
  bool mapped;
};

DATA(Call) {
  NODE(Identifier) entry;
  NODES(Kwarg) kwargs;
  MAPPED(Path) binds;
};

DATA(Link) {
  NODE(Path) target;
  VARIANT(Path, Call) source;
};

DATA(Channel) {
  NODES(Link) links;
};

}

#endif // AUTOCOG_COMPILER_STL_AST_CHANNEL_HXX
