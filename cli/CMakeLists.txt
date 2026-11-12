# Source files for Device Tree Explorer CLI
set(CLI_SOURCES
    main.cpp
    CLIApp.cpp
)

# Create the CLI executable
add_executable(dte-cli ${CLI_SOURCES})

target_link_libraries(dte-cli dte-core)

# Set output directory
set_target_properties(dte-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
