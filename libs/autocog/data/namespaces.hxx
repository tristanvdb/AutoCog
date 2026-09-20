#ifndef AUTOCOG_DATA_NAMESPACES_HXX
#define AUTOCOG_DATA_NAMESPACES_HXX

#include "autocog/data/expr.hxx"

#include <string>

namespace autocog::data::namespaces {

/// The system namespaces: everything an expression can name that is NOT a
/// symbol of the program. Each field says what it is and, crucially, WHEN it
/// can be bound -- the syntax is picked when a prompt is instantiated, the
/// model when it runs, so a reference to either cannot be folded at compile
/// time and must survive as a reference until its stage.
///
/// This table is the single source of truth for that. It replaces the
/// hand-written checks that each consumer used to carry: the stop-predicate's
/// `known_stop_scalar`, the model-property fold in the llama backend, the
/// `__search__.` string-prefix test in the instantiator, and the field lookup
/// in the Python channel layer.
///
///   __syntax__   the rendering knobs (data::Syntax)            instantiate
///   __search__   search parameters, by "<category>.<key>"      instantiate
///   __program__  reserved; no fields yet                        --
///   __model__    properties of the bound model                 execute
///   __status__   statistics of the running evaluation          execute
///
/// A bare `__syntax__` (no field) is not a value: there is no aggregate type
/// in the expression language, only scalars.

enum class FieldType { Bool, Int, Float, Str };

char const * type_word(FieldType t);

/// True if @p ref names a field of a known namespace.
bool known(std::string const & ref);

/// The stage at which @p ref can be bound. Throws CompileError for an unknown
/// namespace or an unknown field in a known one.
Stage stage_of(std::string const & ref,
               std::optional<autocog::location::SourceRange> const & loc = std::nullopt);

/// The declared type of @p ref, for compile-time checking of the expressions
/// built around it. Same errors as stage_of.
FieldType type_of(std::string const & ref,
                  std::optional<autocog::location::SourceRange> const & loc = std::nullopt);

/// stage_of() for its errors only -- call it where a reference is created, so
/// that a mistyped knob fails in the compiler rather than rendering as nothing.
void check(std::string const & ref,
           std::optional<autocog::location::SourceRange> const & loc = std::nullopt);

/// The root namespace of @p ref ("__syntax__"), or "" if it has none.
std::string root_of(std::string const & ref);

/// True for a `__dunder__` word.
bool is_namespace_word(std::string const & word);

/// Qualify a legacy unqualified name: the evaluation scalars (`terminals`,
/// `coverage.fta`, ...) and model properties (`model.n_ctx`) that the
/// termination predicate named bare before the namespaces existed. Returns the
/// name unchanged when it is not one of those.
std::string qualify(std::string const & bare);

/// Comma-separated field list of a namespace, for diagnostics.
std::string fields_of(std::string const & ns);

}

#endif // AUTOCOG_DATA_NAMESPACES_HXX
