# stlc -D / file-scoped argument handling.
#
# These assert the INTENDED behavior of file-scoped arguments and -D:
#   - argument with a default -> usable (referenced or not), default applied
#   - argument without a default, not supplied -> error (required, not supplied)
#   - argument supplied via -D -> value applies
#   - -D targeting a `define` -> warning (-D ignored; define is not a parameter)
# check_stlc_behavior.py classifies the invocation (ok / warning / error) and
# always fails on "internal error".
#
# NOTE: some of these encode behavior that is not yet implemented; they are the
# spec for the fix and are expected to fail until it lands.

set(FIX ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set(CHECK ${PROJECT_SOURCE_DIR}/tests/scripts/check_stlc_behavior.py)

# Helper for the simple "should compile cleanly" (ok) cases.
function(add_arg_ok name fixture)
    add_test(
        NAME stlc_arg_${name}
        COMMAND ${CMAKE_COMMAND} -E env bash -c "
            OUT=$($<TARGET_FILE:autocog_stlc> --emit sta ${FIX}/${fixture} 2>&1) ;
            RC=\$? ;
            echo \"\$OUT\" | python3 ${CHECK} \$RC ok
        "
    )
    set_tests_properties(stlc_arg_${name} PROPERTIES LABELS "stlc;args")
endfunction()

# --- the matrix ---

# argument with default, referenced -> works (ok)
add_arg_ok(default_used      arg_default_used.stl)

# argument with default, unused -> works (ok)
add_arg_ok(default_unused    arg_default_unused.stl)

# argument with default, overridden by -D -> works (ok)
add_test(NAME stlc_arg_default_overridden
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             OUT=$($<TARGET_FILE:autocog_stlc> --emit sta -Dwidth=12 ${FIX}/arg_default_used.stl 2>&1) ;
             RC=\$? ;
             echo \"\$OUT\" | python3 ${CHECK} \$RC ok
         ")
set_tests_properties(stlc_arg_default_overridden PROPERTIES LABELS "stlc;args")

# argument without default, not supplied -> error (required, not supplied)
add_test(NAME stlc_arg_required_missing
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             OUT=$($<TARGET_FILE:autocog_stlc> --emit sta ${FIX}/arg_required_missing.stl 2>&1) ;
             RC=\$? ;
             echo \"\$OUT\" | python3 ${CHECK} \$RC error 'width'
         ")
set_tests_properties(stlc_arg_required_missing PROPERTIES LABELS "stlc;args")

# argument without default, supplied via -D -> works (ok)
add_test(NAME stlc_arg_required_supplied
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             OUT=$($<TARGET_FILE:autocog_stlc> --emit sta -Dwidth=10 ${FIX}/arg_required_missing.stl 2>&1) ;
             RC=\$? ;
             echo \"\$OUT\" | python3 ${CHECK} \$RC ok
         ")
set_tests_properties(stlc_arg_required_supplied PROPERTIES LABELS "stlc;args")

# -D targeting a define -> warning (-D ignored)
add_test(NAME stlc_arg_define_D_warns
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             OUT=$($<TARGET_FILE:autocog_stlc> --emit sta -Dwidth=12 ${FIX}/def_for_D.stl 2>&1) ;
             RC=\$? ;
             echo \"\$OUT\" | python3 ${CHECK} \$RC warning 'width'
         ")
set_tests_properties(stlc_arg_define_D_warns PROPERTIES LABELS "stlc;args")
