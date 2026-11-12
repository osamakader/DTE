find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(WARNING "GoogleTest not found - tests will not be built")
    return()
endif()

add_executable(dte-tests
    DTBParserTest.cpp
    DTBIntegrationTest.cpp
)

target_link_libraries(dte-tests PRIVATE dte-core GTest::gtest GTest::gtest_main)

target_compile_definitions(dte-tests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
if(DTC_EXECUTABLE)
    target_compile_definitions(dte-tests PRIVATE DTC_PATH="${DTC_EXECUTABLE}")
endif()

include(GoogleTest)
gtest_discover_tests(dte-tests)
