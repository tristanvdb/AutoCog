#ifndef AUTOCOG_PARSER_STL_IR_ANNOT_HXX
#define AUTOCOG_PARSER_STL_IR_ANNOT_HXX

namespace autocog { namespace parser {

DATA(Annotation) {
  ONODE(Path) path;
  NODE(String) description;
};

DATA(Annotate) {
  bool single_statement; //< Keyword followed by single element instead of block
  NODES(Annotation) annotations;
};

} }

#endif // AUTOCOG_PARSER_STL_IR_ANNOT_HXX
