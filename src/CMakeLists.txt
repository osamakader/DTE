# Core library shared by the GUI, CLI and tests (no Qt dependency)
add_library(dte-core STATIC
    DeviceTree.cpp
    DeviceTreeParser.cpp
    Process.cpp
    DtsValue.cpp
    Validation.cpp
    References.cpp
    Overlay.cpp
    DeviceTreeDiff.cpp
)

target_include_directories(dte-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# GUI widgets as a library so the application and GUI tests share them
add_library(dte-gui STATIC
    MainWindow.cpp
    DeviceTreeWidgetItem.cpp
    PropertyTableWidget.cpp
    DiffViewerWidget.cpp
    EditCommands.cpp
    PropertyDialog.cpp
    ../include/MainWindow.h
    ../include/PropertyTableWidget.h
    ../include/DiffViewerWidget.h
    ../include/EditCommands.h
    ../include/PropertyDialog.h
)

target_link_libraries(dte-gui PUBLIC
    dte-core
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
)

# Main GUI executable
add_executable(DeviceTreeExplorer main.cpp)
target_link_libraries(DeviceTreeExplorer dte-gui dte-cli-app)

# Set output directory
set_target_properties(DeviceTreeExplorer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
