find_package(instrument-script-server CONFIG REQUIRED)
find_package(GTest                    CONFIG REQUIRED)
include(GoogleTest)

message(STATUS "Building mock instrument plugins...")
add_instrument_plugin(mock_multimeter
  SOURCES plugins/mock_multimeter.c
)
add_instrument_plugin(mock_voltage_source
  SOURCES plugins/mock_voltage_source.c
)

set(TEST_SOURCES
  data-retrieval.cpp
)
add_executable(integration_tests ${TEST_SOURCES})
target_link_libraries(integration_tests
  PRIVATE
    GTest::gtest
    GTest::gtest_main
    instrument-script-server::instrument-script-server-core
)
target_compile_definitions(integration_tests
  PRIVATE
    TEST_DATA_DIR="${CMAKE_BINARY_DIR}/test-data"
    PLUGIN_OUTPUT_DIR="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
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
