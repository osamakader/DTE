# Core library shared by the GUI, CLI and tests (no Qt dependency)
add_library(dte-core STATIC
    DeviceTree.cpp
    DeviceTreeParser.cpp
    DeviceTreeDiff.cpp
)

target_include_directories(dte-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Link JSON and YAML libraries if available
if(nlohmann_json_FOUND)
    target_link_libraries(dte-core PUBLIC nlohmann_json::nlohmann_json)
endif()

if(yaml-cpp_FOUND)
    target_link_libraries(dte-core PUBLIC yaml-cpp)
endif()

# Source files for Device Tree Explorer GUI
set(GUI_SOURCES
    main.cpp
    MainWindow.cpp
    DeviceTreeWidgetItem.cpp
    PropertyTableWidget.cpp
    DiffViewerWidget.cpp
)

# Header files for MOC
set(GUI_HEADERS
    ../include/MainWindow.h
    ../include/PropertyTableWidget.h
    ../include/DiffViewerWidget.h
)

# Create the main GUI executable
add_executable(DeviceTreeExplorer ${GUI_SOURCES} ${GUI_HEADERS})

target_link_libraries(DeviceTreeExplorer
    dte-core
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
)

# Set output directory
set_target_properties(DeviceTreeExplorer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
