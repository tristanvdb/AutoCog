
add_library(autocog_runtime_store SHARED
    store.cxx
)

target_include_directories(autocog_runtime_store PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
)

target_link_libraries(autocog_runtime_store PUBLIC
    autocog_runtime_sta_lib
)

install(TARGETS autocog_runtime_store DESTINATION autocog)
