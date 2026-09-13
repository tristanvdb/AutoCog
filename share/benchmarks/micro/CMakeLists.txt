add_executable(autocog_sampling_bench sampling-bench.cxx)

target_include_directories(autocog_sampling_bench PRIVATE
    ${PROJECT_SOURCE_DIR}/libs
)

set_target_properties(autocog_sampling_bench PROPERTIES
    OUTPUT_NAME sampling_bench
    CXX_STANDARD 20
)
