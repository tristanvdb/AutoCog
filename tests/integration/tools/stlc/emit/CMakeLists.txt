# stlc --emit target tests (all targets on representative fixtures)

set(FIXTURES ${PROJECT_SOURCE_DIR}/tests/fixtures/stl)
set(TEST_FILE ${FIXTURES}/language/structures/test_basic_record.stl)

foreach(target ast symbols globals graph ir sta)
    add_test(NAME stlc_emit_${target}
             COMMAND $<TARGET_FILE:autocog_stlc> --emit ${target} ${TEST_FILE})
    set_tests_properties(stlc_emit_${target} PROPERTIES LABELS "stlc;emit")
endforeach()

# Also test with imports
add_test(NAME stlc_emit_sta_import
         COMMAND $<TARGET_FILE:autocog_stlc> --emit sta -I ${FIXTURES}/language/symbols ${FIXTURES}/language/symbols/test_import.stl)
set_tests_properties(stlc_emit_sta_import PROPERTIES LABELS "stlc;emit")

# ============================================================================
# CLI argument tests — exercise args.cxx code paths
# ============================================================================

# --version
add_test(NAME stlc_args_version
         COMMAND $<TARGET_FILE:autocog_stlc> --version)
set_tests_properties(stlc_args_version PROPERTIES LABELS "stlc;args")

# -v (short form)
add_test(NAME stlc_args_version_short
         COMMAND $<TARGET_FILE:autocog_stlc> -v)
set_tests_properties(stlc_args_version_short PROPERTIES LABELS "stlc;args")

# -i input flag (alternative to positional)
add_test(NAME stlc_args_input_flag
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -i ${TEST_FILE})
set_tests_properties(stlc_args_input_flag PROPERTIES LABELS "stlc;args")

# -o output to file
add_test(NAME stlc_args_output_file
         COMMAND ${CMAKE_COMMAND} -E env bash -c "
             TMPF=$(mktemp) &&
             $<TARGET_FILE:autocog_stlc> --emit ir -o \$TMPF ${TEST_FILE} &&
             test -s \$TMPF && rm -f \$TMPF
         ")
set_tests_properties(stlc_args_output_file PROPERTIES LABELS "stlc;args")

# -V verbose
add_test(NAME stlc_args_verbose
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -V ${TEST_FILE})
set_tests_properties(stlc_args_verbose PROPERTIES LABELS "stlc;args")

# -D define: bool (flag only)
add_test(NAME stlc_args_define_bool
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -Dflag ${TEST_FILE})
set_tests_properties(stlc_args_define_bool PROPERTIES LABELS "stlc;args")

# -D define: int
add_test(NAME stlc_args_define_int
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -Dcount=5 ${TEST_FILE})
set_tests_properties(stlc_args_define_int PROPERTIES LABELS "stlc;args")

# -D define: float
add_test(NAME stlc_args_define_float
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -Dpi=3.14 ${TEST_FILE})
set_tests_properties(stlc_args_define_float PROPERTIES LABELS "stlc;args")

# -D define: string
add_test(NAME stlc_args_define_string
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir "-Dname=\"hello\"" ${TEST_FILE})
set_tests_properties(stlc_args_define_string PROPERTIES LABELS "stlc;args")

# -D define: explicit type
add_test(NAME stlc_args_define_typed
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir -Dval:int=42 ${TEST_FILE})
set_tests_properties(stlc_args_define_typed PROPERTIES LABELS "stlc;args")

# Multiple -I paths
add_test(NAME stlc_args_multi_include
         COMMAND $<TARGET_FILE:autocog_stlc> --emit sta
             -I ${FIXTURES}/language/symbols
             -I ${FIXTURES}/language/structures
             ${FIXTURES}/language/symbols/test_import.stl)
set_tests_properties(stlc_args_multi_include PROPERTIES LABELS "stlc;args")

# Multiple input files
add_test(NAME stlc_args_multi_input
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir
             ${TEST_FILE}
             ${FIXTURES}/language/formats/test_text_format.stl)
set_tests_properties(stlc_args_multi_input PROPERTIES LABELS "stlc;args")

# Long flag forms
add_test(NAME stlc_args_long_flags
         COMMAND $<TARGET_FILE:autocog_stlc> --emit ir
             --input ${TEST_FILE}
             --verbose)
set_tests_properties(stlc_args_long_flags PROPERTIES LABELS "stlc;args")

# --verbose level tests
add_test(NAME stlc_verbose_debug
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_stlc> --emit sta -V debug ${TEST_FILE} 2>&1 >/dev/null | grep -q '\\[D\\]'")
set_tests_properties(stlc_verbose_debug PROPERTIES LABELS "stlc;logging")

add_test(NAME stlc_verbose_error_silent
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "test -z \"$($<TARGET_FILE:autocog_stlc> --emit sta -V error ${TEST_FILE} 2>&1 >/dev/null)\"")
set_tests_properties(stlc_verbose_error_silent PROPERTIES LABELS "stlc;logging")

add_test(NAME stlc_verbose_trace
         COMMAND ${CMAKE_COMMAND} -E env
             bash -c "$<TARGET_FILE:autocog_stlc> --emit sta --verbose trace ${TEST_FILE} 2>&1 >/dev/null | grep -q '\\[T\\]\\|\\[D\\]'")
set_tests_properties(stlc_verbose_trace PROPERTIES LABELS "stlc;logging")
