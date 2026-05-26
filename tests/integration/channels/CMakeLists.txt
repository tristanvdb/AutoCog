# Channel and clause integration tests
#
# These test files exercise all channel types and clause modifiers.
# Compilation tests verify they produce valid STA.
# Orchestration tests (Python) will be added with the Python layer.

function(add_channel_compile_test test_name stl_file)
    add_test(
        NAME channel_${test_name}
        COMMAND $<TARGET_FILE:autocog_stlc> --emit sta ${CMAKE_CURRENT_SOURCE_DIR}/${stl_file}
    )
    set_tests_properties(channel_${test_name} PROPERTIES LABELS "stlc;channel")
endfunction()

# Input and dataflow
add_channel_compile_test(input_dataflow    test_input_dataflow.stl)
add_channel_compile_test(self_dataflow     test_self_dataflow.stl)
add_channel_compile_test(dataflow_ravel    test_dataflow_ravel.stl)
add_channel_compile_test(dataflow_wrap     test_dataflow_wrap.stl)
add_channel_compile_test(dataflow_prune    test_dataflow_prune.stl)

# Call channels
add_channel_compile_test(call_basic        test_call_basic.stl)
add_channel_compile_test(call_mapped       test_call_mapped.stl)
add_channel_compile_test(call_bind         test_call_bind.stl)
add_channel_compile_test(call_mapped_ravel test_call_mapped_ravel.stl)
add_channel_compile_test(call_full_pattern test_call_full_pattern.stl)

# Kwarg-level clauses
add_channel_compile_test(kwarg_ravel       test_kwarg_ravel.stl)
add_channel_compile_test(kwarg_wrap        test_kwarg_wrap.stl)
