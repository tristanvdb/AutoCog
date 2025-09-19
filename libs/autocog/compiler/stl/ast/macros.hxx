
#define EXPAND(...) __VA_ARGS__
#define FOR_EACH_1(what, x) what(x)
#define FOR_EACH_2(what, x, ...) what(x), EXPAND(FOR_EACH_1(what, __VA_ARGS__))
#define FOR_EACH_3(what, x, ...) what(x), EXPAND(FOR_EACH_2(what, __VA_ARGS__))
#define FOR_EACH_4(what, x, ...) what(x), EXPAND(FOR_EACH_3(what, __VA_ARGS__))
#define FOR_EACH_5(what, x, ...) what(x), EXPAND(FOR_EACH_4(what, __VA_ARGS__))
#define FOR_EACH_6(what, x, ...) what(x), EXPAND(FOR_EACH_5(what, __VA_ARGS__))
#define FOR_EACH_7(what, x, ...) what(x), EXPAND(FOR_EACH_6(what, __VA_ARGS__))
#define FOR_EACH_8(what, x, ...) what(x), EXPAND(FOR_EACH_7(what, __VA_ARGS__))
#define FOR_EACH_9(what, x, ...) what(x), EXPAND(FOR_EACH_8(what, __VA_ARGS__))

#define GET_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,_9,NAME,...) NAME
#define FOR_EACH(action, ...) \
  GET_MACRO(__VA_ARGS__, FOR_EACH_9, FOR_EACH_8, FOR_EACH_7, FOR_EACH_6, FOR_EACH_5, \
            FOR_EACH_4, FOR_EACH_3, FOR_EACH_2, FOR_EACH_1)(action, __VA_ARGS__)

#define TAG_PREFIX(x) Tag::x
#define FOR_EACH_TAG(...) FOR_EACH(TAG_PREFIX, __VA_ARGS__)

#define NODE(tag) ::autocog::compiler::stl::ast::node_t<Tag::tag>
#define PNODE(tag) ::autocog::compiler::stl::ast::pnode_t<Tag::tag>
#define ONODE(tag) ::autocog::compiler::stl::ast::onode_t<Tag::tag>
#define NODES(tag) ::autocog::compiler::stl::ast::nodes_t<Tag::tag>
#define PNODES(tag) ::autocog::compiler::stl::ast::pnodes_t<Tag::tag>
#define VARIANT(...) ::autocog::compiler::stl::ast::variant_t<FOR_EACH_TAG(__VA_ARGS__)>
#define VARIANTS(...) ::autocog::compiler::stl::ast::variants_t<FOR_EACH_TAG(__VA_ARGS__)>

#define DATA(tag) template <> struct Data< Tag::tag >

#define EXPAND_STMT(...) __VA_ARGS__
#define FOR_EACH_STMT_1(what, x) what(x);
#define FOR_EACH_STMT_2(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_1(what, __VA_ARGS__))
#define FOR_EACH_STMT_3(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_2(what, __VA_ARGS__))
#define FOR_EACH_STMT_4(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_3(what, __VA_ARGS__))
#define FOR_EACH_STMT_5(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_4(what, __VA_ARGS__))
#define FOR_EACH_STMT_6(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_5(what, __VA_ARGS__))
#define FOR_EACH_STMT_7(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_6(what, __VA_ARGS__))
#define FOR_EACH_STMT_8(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_7(what, __VA_ARGS__))
#define FOR_EACH_STMT_9(what, x, ...) what(x); EXPAND_STMT(FOR_EACH_STMT_8(what, __VA_ARGS__))

#define GET_MACRO_STMT(_1,_2,_3,_4,_5,_6,_7,_8,_9,NAME,...) NAME
#define FOR_EACH_STMT(action, ...) \
  GET_MACRO_STMT(__VA_ARGS__, FOR_EACH_STMT_9, FOR_EACH_STMT_8, FOR_EACH_STMT_7, \
                 FOR_EACH_STMT_6, FOR_EACH_STMT_5, FOR_EACH_STMT_4, \
                 FOR_EACH_STMT_3, FOR_EACH_STMT_2, FOR_EACH_STMT_1)(action, __VA_ARGS__)

#define TRAVERSE_FIELD(field) traverse_generic(traversal, data.field);

#define TRAVERSE_CHILDREN(tag, ...) \
  template <> \
  template <class TraversalT> \
  void Node<Tag::tag>::traverse_children(TraversalT & traversal) const { \
    FOR_EACH_STMT(TRAVERSE_FIELD, __VA_ARGS__) \
  }

#define TRAVERSE_CHILDREN_EMPTY(tag) \
  template <> \
  template <class TraversalT> \
  void Node<Tag::tag>::traverse_children(TraversalT &) const {}

