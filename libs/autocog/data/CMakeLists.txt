# Core data types + content hashing. No nlohmann, no Python: JSON/Python
# conversion lives in autocog::codec (libs/autocog/codec).
add_library(autocog_data_lib STATIC
    utility.cxx
    vocab.cxx
    path.cxx
    channel.cxx
    sta.cxx
    syntax.cxx
    search.cxx
    ftt.cxx
    fta.cxx
)

target_include_directories(autocog_data_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
)

set_property(TARGET autocog_data_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

# The process-wide artifact pool behind the Python bindings. It MUST be a single
# shared instance across every binding module, so datastore() lives in a SHARED
# library that all bindings link (a per-module static copy would give each its
# own pool). The shared logger is bundled here for the same single-instance
# reason, so the Python log sink installed by one module is seen by all.
add_library(autocog_data_store SHARED
    store.cxx
    ${PROJECT_SOURCE_DIR}/libs/autocog/logging.cxx
)
target_include_directories(autocog_data_store PUBLIC
    ${PROJECT_SOURCE_DIR}/libs
    ${PROJECT_SOURCE_DIR}/vendors/headers
)
target_link_libraries(autocog_data_store PUBLIC autocog_data_lib)
set_property(TARGET autocog_data_store PROPERTY POSITION_INDEPENDENT_CODE ON)
install(TARGETS autocog_data_store DESTINATION autocog)
