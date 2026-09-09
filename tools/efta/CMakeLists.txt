###################################################################################################
# Encoder of frames into Finite Thoughts Trees (inverse of psta)

add_executable(autocog_efta
  main.cxx
  ${PROJECT_SOURCE_DIR}/libs/autocog/logging.cxx
)
target_include_directories(autocog_efta PRIVATE
  ${PROJECT_SOURCE_DIR}/libs
  ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_efta PUBLIC
  autocog_runtime_sta_lib
  autocog_backend_llama_lib
  autocog_codec_json
  autocog_utilities_lib
)
set_target_properties(autocog_efta PROPERTIES
  OUTPUT_NAME "efta"
)
if(SKBUILD_SCRIPTS_DIR)
  install(TARGETS autocog_efta DESTINATION ${SKBUILD_SCRIPTS_DIR})
else()
  install(TARGETS autocog_efta DESTINATION bin)
endif()

add_test(NAME efta_smoke_test
         COMMAND efta --help)
set_tests_properties(efta_smoke_test PROPERTIES
    LABELS "smoke;efta"
)
