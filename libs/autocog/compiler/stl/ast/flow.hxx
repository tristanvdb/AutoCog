#ifndef AUTOCOG_COMPILER_STL_AST_FLOW_HXX
#define AUTOCOG_COMPILER_STL_AST_FLOW_HXX

namespace autocog::compiler::stl::ast {

DATA(Edge) {
  NODE(ObjectRef) prompt;
  ONODE(Expression) limit;
  ONODE(Expression) label;
};
TRAVERSE_CHILDREN(Edge, prompt, limit, label)

DATA(Flow) {
  bool short_form;
  NODES(Edge) edges;
};
TRAVERSE_CHILDREN(Flow, edges)

/**
 * Short form:
 *   `flow my_prompt;`
 *   `flow my_prompt[3] as "XXX";`
 * is equivalent to:
 *   `flow { my_prompt; }`
 *   `flow { my_prompt[3] as "XXX"; }`
 * 
 */

}

#endif // AUTOCOG_COMPILER_STL_AST_FLOW_HXX
