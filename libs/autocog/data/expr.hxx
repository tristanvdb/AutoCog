#ifndef AUTOCOG_DATA_EXPR_HXX
#define AUTOCOG_DATA_EXPR_HXX

#include "autocog/utilities/types.hxx"
#include "autocog/utilities/errors.hxx"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace autocog::data {

using autocog::types::Value;

// ---------------------------------------------------------------------------
// One expression language
// ---------------------------------------------------------------------------
// Every expression AutoCog carries -- an STL initializer, a field annotation, a
// search parameter, a termination predicate, a call argument -- is a tree of
// these nodes. There is exactly one representation, one set of operator
// semantics, one evaluator, and one partial evaluator, shared by the compiler
// (stlc), the instantiator (ista) and the backend (xfta).
//
// The point of that sharing is STAGING. An expression is written in STL, where
// some of what it names is known (a define, an argument, a -D) and some is not:
// the syntax is chosen when a prompt is instantiated, the model when it runs.
// `residual()` folds everything a given stage can resolve and returns what is
// left, so the same tree is narrowed as it travels and each stage only ever
// sees references it can actually bind. An expression that resolves fully at
// compile time -- which is nearly all of them -- comes out as a Lit and costs
// nothing downstream.
//
// The unparse forms are str() (STL-parseable, used for hashing and for
// STL* output) and to_compact()/from_compact() (s-expression, used where a
// tree must ride a scalar-only channel). Neither is the machine carry: that is
// always the structured tree, via autocog::codec.

enum class OpKind {
    NOP,                         // Not an operator
    Not, Neg,                    // Unary
    Add, Sub, Mul, Div, Mod,     // Arithmetic
    And, Or,                     // Logical
    BAnd, BOr,                   // Set/bitwise (vocab: intersection, union)
    Lt, Gt, Lte, Gte, Eq, Neq    // Comparison
};

/// Debug/diagnostic name ("Add"). The wire and compact forms use op_word().
std::string opKindToString(OpKind op);

/// Lowercase wire name ("add", "lte", ...): the JSON discriminant and the head
/// of the compact s-expression. Chosen over the symbol so that the compact form
/// needs no operator-precedence parser.
char const * op_word(OpKind op);
bool op_from_word(std::string const & word, OpKind & out);

/// Source spelling ("+", "<=", ...), used by str().
char const * op_symbol(OpKind op);

/// True for the operators that take a single operand.
bool op_is_unary(OpKind op);

/// True for the operators that are associative and accept more than two
/// operands in the tree (`all`/`any` fold into these).
bool op_is_variadic(OpKind op);

// ---------------------------------------------------------------------------
// Stages
// ---------------------------------------------------------------------------
// When a reference can be bound. A slot that accepts an expression declares the
// latest stage it can serve, and an expression whose references outlive that
// slot is a compile error rather than a runtime surprise.

enum class Stage : unsigned {
    Compile     = 0,   ///< stlc: defines, arguments, -D. Nothing survives.
    Instantiate = 1,   ///< ista: __syntax__, __search__, __program__.
    Execute     = 2    ///< xfta: __model__, __status__.
};

char const * stage_word(Stage s);

/// The stage at which a namespaced reference can be bound. Unknown roots throw;
/// the set of namespaces and their fields lives in namespaces.hxx.
Stage ref_stage(std::string const & ref);

// ---------------------------------------------------------------------------
// The node
// ---------------------------------------------------------------------------

struct Expr {
    enum class Kind {
        Lit,    ///< A resolved scalar. `value` holds it; operands empty.
        Ref,    ///< A namespaced reference ("__syntax__.prompt_zero_index").
        Op,     ///< `op` applied to operands (1 for unary, 2, or n for variadic).
        Cond,   ///< operands[0] ? operands[1] : operands[2].
        Cat     ///< String concatenation of the stringified operands (f-strings).
    };

    Kind kind = Kind::Lit;
    Value value{};                  ///< Lit
    std::string ref;                ///< Ref
    OpKind op = OpKind::NOP;        ///< Op
    std::vector<Expr> operands;     ///< Op / Cond / Cat

    Expr() : value(std::monostate{}) {}

    static Expr lit(Value v);
    static Expr lit(int v);
    static Expr lit(float v);
    static Expr lit(bool v);
    static Expr lit(std::string v);
    static Expr reference(std::string r);
    static Expr unary(OpKind op, Expr operand);
    static Expr binary(OpKind op, Expr lhs, Expr rhs);
    static Expr variadic(OpKind op, std::vector<Expr> operands);
    static Expr conditional(Expr cond, Expr e_true, Expr e_false);
    static Expr concat(std::vector<Expr> parts);

    bool is_literal() const { return kind == Kind::Lit; }

    /// The value, when this is a literal. nullopt means "still deferred" -- the
    /// check every slot that needs a constant performs.
    std::optional<Value> as_value() const;

    /// Every reference in the subtree, in tree order, duplicates included.
    void collect_refs(std::vector<std::string> & out) const;

    /// The latest stage any reference in the subtree needs; Compile when the
    /// subtree is reference-free.
    Stage stage() const;

    /// Recursive content hash. An expression is part of what a program MEANS,
    /// so it is part of the program's identity: two programs that differ only
    /// in a deferred expression are different programs.
    std::string hash() const;

    /// STL-parseable unparse, fully parenthesized. Round-trips through the STL
    /// expression parser.
    std::string str() const;

    /// S-expression form, for the channels that carry scalars only.
    ///   (add @__syntax__.prompt_indent "x")   5   "text"   @__model__.n_ctx
    std::string to_compact() const;
    static Expr from_compact(std::string const & text);

    bool operator==(Expr const & other) const;
    bool operator!=(Expr const & other) const { return !(*this == other); }
};

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

/// Binds a namespaced reference. Returning nullopt means "not mine": the
/// reference stays deferred (in residual()) or is an error (in evaluate()).
/// Throwing rejects it outright -- a resolver that owns a namespace uses that
/// to report an unknown field in it.
using Resolver = std::function<std::optional<Value>(std::string const & ref)>;

/// A resolver that binds nothing: the Compile stage, where only the values
/// already folded into the tree are available.
Resolver const & no_refs();

/// Partial evaluation. Folds every node whose subtree the resolver can bind and
/// returns what is left: literals collapse, a Cond with a known condition drops
/// the untaken branch, a Cat merges adjacent literal parts. The result is a
/// Lit iff every reference was resolved.
///
/// This is the whole mechanism. stlc calls it with the symbol table, ista with
/// the syntax, xfta with the running state; each hands on a smaller tree.
Expr residual(Expr const & e, Resolver const & resolve,
              std::optional<autocog::location::SourceRange> const & loc = std::nullopt);

/// residual() plus the requirement that nothing is left. Throws CompileError
/// naming the first unresolved reference.
Value evaluate(Expr const & e, Resolver const & resolve,
               std::optional<autocog::location::SourceRange> const & loc = std::nullopt);

/// evaluate() into text, using the same stringification as an f-string field.
std::string render(Expr const & e, Resolver const & resolve,
                   std::optional<autocog::location::SourceRange> const & loc = std::nullopt);

/// Value -> text: integers plain, floats without trailing zeros, booleans as
/// `true`/`false`, null as empty. The single definition of what a value looks
/// like when it lands in a prompt.
std::string stringify(Value const & v);

/// Operator semantics, shared by compile-time folding and every later stage.
/// The location is carried only for diagnostics.
Value apply_unary(OpKind op, Value const & operand,
                  std::optional<autocog::location::SourceRange> const & loc = std::nullopt);
Value apply_binary(OpKind op, Value const & lhs, Value const & rhs,
                   std::optional<autocog::location::SourceRange> const & loc = std::nullopt);

}

#endif // AUTOCOG_DATA_EXPR_HXX
