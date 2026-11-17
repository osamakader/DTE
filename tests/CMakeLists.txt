find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(WARNING "GoogleTest not found - tests will not be built")
    return()
endif()

add_executable(dte-tests
    DTBParserTest.cpp
    DTBIntegrationTest.cpp
    DTSParserTest.cpp
    DiffTest.cpp
    ExportTest.cpp
    CLITest.cpp
    WriterTest.cpp
    SearchTest.cpp
    DtsValueTest.cpp
    NodeTest.cpp
    ProcessTest.cpp
    PropertyTest.cpp
)

target_link_libraries(dte-tests PRIVATE dte-core GTest::gtest GTest::gtest_main)

# Optional: parse exported JSON/YAML with real libraries
if(nlohmann_json_FOUND)
    target_link_libraries(dte-tests PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(dte-tests PRIVATE HAVE_NLOHMANN_JSON)
endif()
if(yaml-cpp_FOUND)
    if(TARGET yaml-cpp::yaml-cpp)
        target_link_libraries(dte-tests PRIVATE yaml-cpp::yaml-cpp)
    else()
        target_link_libraries(dte-tests PRIVATE yaml-cpp)
    endif()
    target_compile_definitions(dte-tests PRIVATE HAVE_YAML_CPP)
endif()

target_compile_definitions(dte-tests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    DTE_CLI_PATH="$<TARGET_FILE:dte-cli>"
)
add_dependencies(dte-tests dte-cli)
if(DTC_EXECUTABLE)
    target_compile_definitions(dte-tests PRIVATE DTC_PATH="${DTC_EXECUTABLE}")
endif()

include(GoogleTest)
gtest_discover_tests(dte-tests)

# Offscreen GUI smoke tests
add_executable(dte-gui-tests GuiTest.cpp)
target_link_libraries(dte-gui-tests PRIVATE dte-gui GTest::gtest)
target_compile_definitions(dte-gui-tests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
if(DTC_EXECUTABLE)
    target_compile_definitions(dte-gui-tests PRIVATE DTC_PATH="${DTC_EXECUTABLE}")
endif()
gtest_discover_tests(dte-gui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
