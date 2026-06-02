# stlc expression-evaluation behavior: bitwise |/& operators and the rejection
# of vocab-only expressions used as values.
#
# Intended behavior:
#   - |/& on integer operands  -> bitwise OR/AND, evaluates to a value (ok)
#   - |/& on float/string/bool -> proper diagnostic "requires integer operands"
#   - tokenize()/regex() as a value -> proper diagnostic (vocab-only)
# check_stlc_behavior.py classifies the invocation (ok/warning/error) and always
# fails on "internal error". Some cases encode not-yet-implemented behavior and
# are the spec for the fix.

set(FIX ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set(CHECK ${PROJECT_SOURCE_DIR}/tests/scripts/check_stlc_behavior.py)

function(add_eval_test name fixture mode substr)
    add_test(
        NAME stlc_eval_${name}
        COMMAND ${CMAKE_COMMAND} -E env bash -c "
            OUT=$($<TARGET_FILE:autocog_stlc> --emit sta ${FIX}/${fixture} 2>&1) ;
            RC=\$? ;
            echo \"\$OUT\" | python3 ${CHECK} \$RC ${mode} '${substr}'
        "
    )
    set_tests_properties(stlc_eval_${name} PROPERTIES LABELS "stlc;eval")
endfunction()

# integer bitwise -> value
add_eval_test(bitor_int     bitor_int.stl     ok      "")
add_eval_test(bitand_int    bitand_int.stl    ok      "")

# non-integer operands -> proper diagnostic
add_eval_test(bitor_float   bitor_float.stl   error   "requires integer operands")
add_eval_test(bitand_string bitand_string.stl error   "requires integer operands")
add_eval_test(bitor_bool    bitor_bool.stl    error   "requires integer operands")

# vocab-only expressions used as a value -> proper diagnostic (vocab-only)
add_eval_test(tokenize_as_value tokenize_as_value.stl error "vocabulary")
add_eval_test(regex_as_value    regex_as_value.stl    error "vocabulary")
