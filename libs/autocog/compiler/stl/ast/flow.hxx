#ifndef AUTOCOG_COMPILER_STL_AST_FLOW_HXX
#define AUTOCOG_COMPILER_STL_AST_FLOW_HXX

namespace autocog::compiler::stl::ast {

DATA(Edge) {
  NODE(PromptRef) prompt;
  ONODE(Expression) limit;
  ONODE(Expression) label;
};

DATA(Flow) {
  bool short_form;
  NODES(Edge) edges;
};

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
