# stlc emit-target tests (each output format on representative fixtures) plus
# CLI argument coverage for args.cxx.

set(FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(TEST_FILE ${FIXTURES}/language/structures/test_basic_record.stl)

# Each emit format, written to /dev/null (smoke: exit 0).
foreach(target ast graph ir sta)
    add_test(NAME stlc_emit_${target}
             COMMAND $<TARGET_FILE:autocog_stlc> --${target} /dev/null ${TEST_FILE})
    set_tests_properties(stlc_emit_${target} PROPERTIES LABELS "stlc;emit")
endforeach()

# The per-format flags compose: emit several artifacts in one run.
add_test(NAME stlc_emit_multi
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             A=$(mktemp) S=$(mktemp) &&
             $<TARGET_FILE:autocog_stlc> --ast \$A --sta \$S ${TEST_FILE} &&
             test -s \$A && test -s \$S && rm -f \$A \$S
         ")
set_tests_properties(stlc_emit_multi PROPERTIES LABELS "stlc;emit")

# Emit AST over a fixture exercising the expression node kinds that survive a
# full compile (Identifier, Integer, Float, Boolean, String, Unary, Binary,
# Conditional, Parenthesis), so serialize/ast.cxx's expr_to_json is covered --
# including the Float/round3 path. Tokenize/Regex are checked via the vocab
# fixture (where they are valid expressions).
add_test(NAME stlc_emit_ast_nodes
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             out=$($<TARGET_FILE:autocog_stlc> --ast /dev/stdout ${FIXTURES}/language/expressions/test_emit_ast_nodes.stl) || exit 1
             for n in Identifier Integer Float Boolean String Unary Binary Conditional Parenthesis; do
                 echo \"\$out\" | grep -q \"\\\"node\\\": \\\"\$n\\\"\" || { echo \"missing node: \$n\"; exit 1; }
             done
             echo \"\$out\" | grep -A1 '\\\"Float\\\"' | grep -q '3.142' || { echo 'Float round3 not applied'; exit 1; }
             voc=$($<TARGET_FILE:autocog_stlc> --ast /dev/stdout ${FIXTURES}/language/vocab/test_vocab.stl) || exit 1
             for n in Tokenize Regex; do
                 echo \"\$voc\" | grep -q \"\\\"node\\\": \\\"\$n\\\"\" || { echo \"missing node: \$n\"; exit 1; }
             done
         ")
set_tests_properties(stlc_emit_ast_nodes PROPERTIES LABELS "stlc;emit")

# Emit STA with imports (-I).
add_test(NAME stlc_emit_sta_import
         COMMAND $<TARGET_FILE:autocog_stlc> --sta /dev/null -I ${FIXTURES}/language/symbols ${FIXTURES}/language/symbols/test_import.stl)
set_tests_properties(stlc_emit_sta_import PROPERTIES LABELS "stlc;emit")

# ============================================================================
# CLI argument tests — exercise args.cxx code paths
# ============================================================================

# --version
add_test(NAME stlc_args_version
         COMMAND $<TARGET_FILE:autocog_stlc> --version)
set_tests_properties(stlc_args_version PROPERTIES LABELS "stlc;args")

# --verbose
add_test(NAME stlc_args_verbose
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null --verbose ${TEST_FILE})
set_tests_properties(stlc_args_verbose PROPERTIES LABELS "stlc;args")

# -D define: bool (flag only)
add_test(NAME stlc_args_define_bool
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -Dflag ${TEST_FILE})
set_tests_properties(stlc_args_define_bool PROPERTIES LABELS "stlc;args")

# -D define: int
add_test(NAME stlc_args_define_int
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -Dcount=5 ${TEST_FILE})
set_tests_properties(stlc_args_define_int PROPERTIES LABELS "stlc;args")

# -D define: float
add_test(NAME stlc_args_define_float
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -Dpi=3.14 ${TEST_FILE})
set_tests_properties(stlc_args_define_float PROPERTIES LABELS "stlc;args")

# -D define: string
add_test(NAME stlc_args_define_string
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null "-Dname=\"hello\"" ${TEST_FILE})
set_tests_properties(stlc_args_define_string PROPERTIES LABELS "stlc;args")

# -D define: explicit type
add_test(NAME stlc_args_define_typed
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null -Dval:int=42 ${TEST_FILE})
set_tests_properties(stlc_args_define_typed PROPERTIES LABELS "stlc;args")

# Include paths: separate -I <path> and compact -I<path> both resolve imports
add_test(NAME stlc_args_multi_include
         COMMAND $<TARGET_FILE:autocog_stlc> --sta /dev/null
             -I ${FIXTURES}/language/symbols
             -I${FIXTURES}/language/structures
             ${FIXTURES}/language/symbols/test_import.stl)
set_tests_properties(stlc_args_multi_include PROPERTIES LABELS "stlc;args")

# Multiple positional input files
add_test(NAME stlc_args_multi_input
         COMMAND $<TARGET_FILE:autocog_stlc> --ir /dev/null
             ${TEST_FILE}
             ${FIXTURES}/language/formats/test_text_format.stl)
set_tests_properties(stlc_args_multi_input PROPERTIES LABELS "stlc;args")

# --verbose level tests
add_test(NAME stlc_verbose_debug
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_stlc> --sta /dev/null --verbose debug ${TEST_FILE} 2>&1 >/dev/null | grep -q '\\[D\\]'")
set_tests_properties(stlc_verbose_debug PROPERTIES LABELS "stlc;logging")

add_test(NAME stlc_verbose_error_silent
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "test -z \"$($<TARGET_FILE:autocog_stlc> --sta /dev/null --verbose error ${TEST_FILE} 2>&1 >/dev/null)\"")
set_tests_properties(stlc_verbose_error_silent PROPERTIES LABELS "stlc;logging")

add_test(NAME stlc_verbose_trace
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_stlc> --sta /dev/null --verbose trace ${TEST_FILE} 2>&1 >/dev/null | grep -q '\\[T\\]\\|\\[D\\]'")
set_tests_properties(stlc_verbose_trace PROPERTIES LABELS "stlc;logging")

# ============================================================================
# Error paths (must exit non-zero)
# ============================================================================

# No emit output requested
add_test(NAME stlc_err_no_output COMMAND $<TARGET_FILE:autocog_stlc> ${TEST_FILE})
set_tests_properties(stlc_err_no_output PROPERTIES WILL_FAIL TRUE LABELS "stlc;args")

# Unknown option
add_test(NAME stlc_err_unknown_opt COMMAND $<TARGET_FILE:autocog_stlc> --bogus --sta /dev/null ${TEST_FILE})
set_tests_properties(stlc_err_unknown_opt PROPERTIES WILL_FAIL TRUE LABELS "stlc;args")

# No input files
add_test(NAME stlc_err_no_input COMMAND $<TARGET_FILE:autocog_stlc> --sta /dev/null)
set_tests_properties(stlc_err_no_input PROPERTIES WILL_FAIL TRUE LABELS "stlc;args")

# Missing argument to an emit flag
add_test(NAME stlc_err_emit_no_arg COMMAND $<TARGET_FILE:autocog_stlc> ${TEST_FILE} --sta)
set_tests_properties(stlc_err_emit_no_arg PROPERTIES WILL_FAIL TRUE LABELS "stlc;args")
