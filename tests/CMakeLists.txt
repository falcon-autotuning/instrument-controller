find_package(instrument-script-server CONFIG REQUIRED)
find_package(GTest                    CONFIG REQUIRED)
find_package(falcon-routine           CONFIG REQUIRED)
include(GoogleTest)

add_library(plugin-helpers INTERFACE)

target_include_directories(plugin-helpers
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/instrument-plugins/plugin-helpers.h
)

message(STATUS "Building mock instrument plugins...")
add_instrument_plugin(mock_multimeter
  SOURCES instrument-plugins/mock-multimeter.c
  LINK_LIBRARIES
    instrument-data::instrument-data
    plugin-helpers
)
add_instrument_plugin(mock_voltage_source
  SOURCES instrument-plugins/mock-voltage-source.c
  LINK_LIBRARIES
    plugin-helpers
)

set(TEST_SOURCES
  instrument-control/data-retrieval.cpp
)
add_executable(integration_tests ${TEST_SOURCES})
target_link_libraries(integration_tests
  PRIVATE
    GTest::gtest
    GTest::gtest_main
    instrument-script-server::instrument-script-server-core
    falcon::falcon-routine
    falcon-core::falcon-core
)
target_compile_definitions(integration_tests
  PRIVATE
    TEST_DATA_DIR="${CMAKE_BINARY_DIR}/test-data"
    PLUGIN_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
set(PLUGIN_SEARCH_PATHS
    "${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}"
    "${CMAKE_BINARY_DIR}/lib/plugins"
    "${CMAKE_BINARY_DIR}"
)
gtest_discover_tests(integration_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  ENVIRONMENT "PATH=${PLUGIN_SEARCH_PATHS};$ENV{PATH}"
)
message(STATUS "Integration tests configured")
message(STATUS "  Test executable: integration_tests")
message(STATUS "  Plugin search path: ${PLUGIN_SEARCH_PATHS}")
