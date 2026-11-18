# Command-line application, as a library so the GUI binary's --cli mode
# can reuse it
add_library(dte-cli-app STATIC CLIApp.cpp)
target_link_libraries(dte-cli-app PUBLIC dte-core)

# dte-cli executable
add_executable(dte-cli main.cpp)
target_link_libraries(dte-cli dte-cli-app)

set_target_properties(dte-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
