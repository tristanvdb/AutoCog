#ifndef AUTOCOG_COMPILER_STL_SERIALIZE_HXX
#define AUTOCOG_COMPILER_STL_SERIALIZE_HXX

#include <iostream>

namespace autocog::compiler::stl {

class Driver;

void serialize_ast(Driver const & driver, std::ostream & out);
void serialize_graph(Driver const & driver, std::ostream & out);
void serialize_ir(Driver const & driver, std::ostream & out);
void serialize_sta(Driver const & driver, std::ostream & out);

}

#endif // AUTOCOG_COMPILER_STL_SERIALIZE_HXX
