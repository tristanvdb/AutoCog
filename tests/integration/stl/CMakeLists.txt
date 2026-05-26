# STL Compiler Integration Tests

# ============================================================================
# IR + unparse tests: full pipeline for every test file
# stlc --emit ir <file> | python unparse-ir.py
# ============================================================================

function(add_stl_test test_name stl_file)
    add_test(
        NAME stl_${test_name}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "$<TARGET_FILE:autocog_stlc> --emit ir -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${stl_file} | ${Python_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/stlc/unparse-ir.py > /dev/null"
    )
    set_tests_properties(stl_${test_name} PROPERTIES LABELS "stlc;ir")
endfunction()

# Basic language features
add_stl_test(basic_record         test_basic_record.stl)
add_stl_test(prompt_with_fields   test_prompt_with_fields.stl)
add_stl_test(defines              test_defines.stl)
add_stl_test(nested_record        test_nested_record.stl)
add_stl_test(alias                test_alias.stl)
add_stl_test(export               test_export.stl)
add_stl_test(import               test_import.stl)

# Channels
add_stl_test(input_channel        test_input_channel.stl)
add_stl_test(dataflow_channel     test_dataflow_channel.stl)

# Flows
add_stl_test(flow_simple          test_flow_simple.stl)
add_stl_test(flow_loop            test_flow_loop.stl)

# Return
add_stl_test(return_labeled       test_return_labeled.stl)

# Arrays
add_stl_test(array_fields         test_array_fields.stl)

# Parameterization
add_stl_test(parameterized_record test_parameterized_record.stl)
add_stl_test(parameterized_prompt test_parameterized_prompt.stl)

# Annotations
add_stl_test(annotations          test_annotations.stl)

# Formats
add_stl_test(unnamed_record       test_unnamed_record.stl)
add_stl_test(text_format          test_text_format.stl)
add_stl_test(choice_formats       test_choice_formats.stl)

# Search
add_stl_test(search               test_search.stl)

# ============================================================================
# Emit stage tests: verify each --emit target with representative files
# ============================================================================

# --emit ast: verify parser output
add_test(NAME emit_ast_simple  COMMAND $<TARGET_FILE:autocog_stlc> --emit ast  ${CMAKE_CURRENT_SOURCE_DIR}/test_basic_record.stl)
add_test(NAME emit_ast_import  COMMAND $<TARGET_FILE:autocog_stlc> --emit ast  -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/test_import.stl)
add_test(NAME emit_ast_complex COMMAND $<TARGET_FILE:autocog_stlc> --emit ast  ${CMAKE_CURRENT_SOURCE_DIR}/test_choice_formats.stl)
set_tests_properties(emit_ast_simple emit_ast_import emit_ast_complex PROPERTIES LABELS "stlc;emit")

# --emit symbols: verify symbol collection
add_test(NAME emit_symbols_simple  COMMAND $<TARGET_FILE:autocog_stlc> --emit symbols  ${CMAKE_CURRENT_SOURCE_DIR}/test_basic_record.stl)
add_test(NAME emit_symbols_import  COMMAND $<TARGET_FILE:autocog_stlc> --emit symbols  -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/test_import.stl)
add_test(NAME emit_symbols_params  COMMAND $<TARGET_FILE:autocog_stlc> --emit symbols  ${CMAKE_CURRENT_SOURCE_DIR}/test_parameterized_record.stl)
set_tests_properties(emit_symbols_simple emit_symbols_import emit_symbols_params PROPERTIES LABELS "stlc;emit")

# --emit globals: verify global evaluation
add_test(NAME emit_globals_defines COMMAND $<TARGET_FILE:autocog_stlc> --emit globals  ${CMAKE_CURRENT_SOURCE_DIR}/test_defines.stl)
add_test(NAME emit_globals_params  COMMAND $<TARGET_FILE:autocog_stlc> --emit globals  ${CMAKE_CURRENT_SOURCE_DIR}/test_parameterized_record.stl)
set_tests_properties(emit_globals_defines emit_globals_params PROPERTIES LABELS "stlc;emit")

# --emit graph: verify instantiation graph
add_test(NAME emit_graph_simple    COMMAND $<TARGET_FILE:autocog_stlc> --emit graph  ${CMAKE_CURRENT_SOURCE_DIR}/test_basic_record.stl)
add_test(NAME emit_graph_import    COMMAND $<TARGET_FILE:autocog_stlc> --emit graph  -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/test_import.stl)
add_test(NAME emit_graph_params    COMMAND $<TARGET_FILE:autocog_stlc> --emit graph  ${CMAKE_CURRENT_SOURCE_DIR}/test_parameterized_record.stl)
add_test(NAME emit_graph_flows     COMMAND $<TARGET_FILE:autocog_stlc> --emit graph  ${CMAKE_CURRENT_SOURCE_DIR}/test_flow_loop.stl)
set_tests_properties(emit_graph_simple emit_graph_import emit_graph_params emit_graph_flows PROPERTIES LABELS "stlc;emit")

# --emit ir: covered by stl_* tests above, add a few explicit ones
add_test(NAME emit_ir_annotations  COMMAND $<TARGET_FILE:autocog_stlc> --emit ir  ${CMAKE_CURRENT_SOURCE_DIR}/test_annotations.stl)
add_test(NAME emit_ir_channels     COMMAND $<TARGET_FILE:autocog_stlc> --emit ir  ${CMAKE_CURRENT_SOURCE_DIR}/test_dataflow_channel.stl)
set_tests_properties(emit_ir_annotations emit_ir_channels PROPERTIES LABELS "stlc;emit")

# --emit sta: expected failure (not yet implemented)
add_test(NAME emit_sta COMMAND $<TARGET_FILE:autocog_stlc> --emit sta ${CMAKE_CURRENT_SOURCE_DIR}/test_basic_record.stl)
set_tests_properties(emit_sta PROPERTIES LABELS "stlc;emit")

# ============================================================================
# STA + ista tests: stlc --emit sta | ista --text
# ============================================================================

function(add_ista_test test_name stl_file)
    add_test(
        NAME ista_${test_name}
        COMMAND ${CMAKE_COMMAND} -E env
            bash -c "$<TARGET_FILE:autocog_stlc> --emit sta -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${stl_file} | $<TARGET_FILE:autocog_ista> --text /dev/stdin > /dev/null"
    )
    set_tests_properties(ista_${test_name} PROPERTIES LABELS "stlc;ista")
endfunction()

add_ista_test(basic_record         test_basic_record.stl)
add_ista_test(prompt_with_fields   test_prompt_with_fields.stl)
add_ista_test(array_fields         test_array_fields.stl)
add_ista_test(flow_loop            test_flow_loop.stl)
add_ista_test(return_labeled       test_return_labeled.stl)
add_ista_test(choice_formats       test_choice_formats.stl)
add_ista_test(annotations          test_annotations.stl)
add_ista_test(import               test_import.stl)
add_ista_test(parameterized_prompt test_parameterized_prompt.stl)
