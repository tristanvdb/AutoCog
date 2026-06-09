# Smoke tests for the autocog::data artifact module.
find_package(Python COMPONENTS Development.Embed REQUIRED)

add_executable(data_smoke_driver data_smoke_driver.cxx)
target_include_directories(data_smoke_driver PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(data_smoke_driver PUBLIC
  autocog_data_lib
  autocog_codec_json
  autocog_codec_python
  autocog_utilities_lib
  Python::Python
)
add_test(
    NAME data_smoke
    COMMAND data_smoke_driver ${PROJECT_SOURCE_DIR}/share
)
set_tests_properties(data_smoke PROPERTIES LABELS "units;data")

add_executable(data_registry_driver registry_driver.cxx)
target_include_directories(data_registry_driver PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(data_registry_driver PUBLIC
  autocog_data_store
  autocog_data_lib
  autocog_utilities_lib
  Python::Python
)
add_test(
    NAME data_registry
    COMMAND data_registry_driver
)
set_tests_properties(data_registry PROPERTIES LABELS "units;data")
