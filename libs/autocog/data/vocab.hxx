#ifndef AUTOCOG_DATA_VOCAB_HXX
#define AUTOCOG_DATA_VOCAB_HXX

#include <string>
#include <vector>

namespace autocog::data {

/// A resolved vocabulary expression: the set-algebra AST describing the token
/// set a completion may emit. Reference-free (all vocab references are inlined
/// during lowering), so the tree holds only leaves (tokenize/regex) and set
/// operations. Shared by FTA and STA. Conversion lives in the free functions
/// in json.hxx / python.hxx.
struct VocabExpr {
  enum class Kind { Tokenize, Regex, Union, Intersect, Diff, Complement };

  Kind kind = Kind::Tokenize;
  std::vector<std::string> strings;   ///< Tokenize: the strings; Regex: [pattern].
  std::vector<VocabExpr> operands;    ///< Union/Intersect/Diff: 2; Complement: 1.

  /// Recursive content hash: digest(kind + strings + each operand's hash).
  std::string hash() const;
};

}

#endif // AUTOCOG_DATA_VOCAB_HXX
