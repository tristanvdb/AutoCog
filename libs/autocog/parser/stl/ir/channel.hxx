#ifndef AUTOCOG_PARSER_STL_IR_CHANNEL_HXX
#define AUTOCOG_PARSER_STL_IR_CHANNEL_HXX

namespace autocog { namespace parser {

DATA(Kwarg) {
  NODE(Identifier) name;
  NODE(Path) source;
  bool mapped;
};

DATA(Call) {
  NODE(Identifier) extcog;
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

} // namespace parser  
} // namespace autocog

#endif // AUTOCOG_PARSER_STL_IR_CHANNEL_HXX
