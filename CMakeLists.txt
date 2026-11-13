cmake_minimum_required(VERSION 3.16)
project(DeviceTreeExplorer VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# Find JSON and YAML libraries
find_package(nlohmann_json 3.2.0 QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "Found nlohmann/json: ${nlohmann_json_VERSION}")
    add_definitions(-DHAVE_NLOHMANN_JSON)
else()
    message(WARNING "nlohmann/json not found - will use custom JSON implementation")
endif()

find_package(yaml-cpp QUIET)
if(yaml-cpp_FOUND)
    message(STATUS "Found yaml-cpp: ${yaml-cpp_VERSION}")
    add_definitions(-DHAVE_YAML_CPP)
else()
    message(WARNING "yaml-cpp not found - will use custom YAML implementation")
endif()

# Find dtc binary (Device Tree Compiler)
find_program(DTC_EXECUTABLE dtc)
if(DTC_EXECUTABLE)
    message(STATUS "Found dtc: ${DTC_EXECUTABLE}")
else()
    message(WARNING "dtc not found - DTS loading and conversion need it at runtime")
    message(STATUS "Install device-tree-compiler package for full functionality")
endif()

# Set compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

option(DTE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(DTE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

# Enable automoc for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(cli)

# Tests (ctest)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Install targets
install(TARGETS DeviceTreeExplorer DESTINATION bin)
install(TARGETS dte-cli DESTINATION bin)

# Install desktop file if it exists
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/DeviceTreeExplorer.desktop)
    install(FILES resources/DeviceTreeExplorer.desktop DESTINATION share/applications)
endif()

# Install icons if they exist
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons)
    install(DIRECTORY resources/icons DESTINATION share/DeviceTreeExplorer)
endif() 