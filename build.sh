#!/bin/bash

# Device Tree Explorer Build Script
# This script automates the build process and provides helpful feedback

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check dependencies
check_dependencies() {
    print_status "Checking dependencies..."
    
    local missing_deps=()
    
    # Check for required tools
    if ! command_exists cmake; then
        missing_deps+=("cmake")
    fi
    
    if ! command_exists make; then
        missing_deps+=("make")
    fi
    
    if ! command_exists g++; then
        missing_deps+=("g++")
    fi
    
    if ! command_exists pkg-config; then
        missing_deps+=("pkg-config")
    fi
    
    # Check for Qt6
    if ! pkg-config --exists Qt6Core Qt6Widgets Qt6Gui; then
        missing_deps+=("Qt6 development packages")
    fi
    
    # Check for dtc
    if ! command_exists dtc; then
        missing_deps+=("device-tree-compiler")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing dependencies:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        echo
        print_warning "Please install the missing dependencies:"
        echo "  Ubuntu/Debian: sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools device-tree-compiler pkg-config"
        echo "  Fedora/RHEL: sudo dnf install gcc-c++ cmake qt6-qtbase-devel device-tree-compiler pkg-config"
        echo "  Arch Linux: sudo pacman -S base-devel cmake qt6-base device-tree-compiler pkg-config"
        exit 1
    fi
    
    print_success "All dependencies found"
}

# Function to detect number of CPU cores
get_jobs() {
    if command_exists nproc; then
        nproc
    elif command_exists sysctl; then
        sysctl -n hw.ncpu
    else
        echo 4  # Default fallback
    fi
}

# Function to build the project
build_project() {
    local build_type=${1:-Release}
    local jobs=${2:-$(get_jobs)}
    
    print_status "Building Device Tree Explorer..."
    print_status "Build type: $build_type"
    print_status "Jobs: $jobs"
    
    # Create build directory
    if [ ! -d "build" ]; then
        print_status "Creating build directory..."
        mkdir build
    fi
    
    cd build
    
    # Configure with CMake
    print_status "Configuring with CMake..."
    cmake -DCMAKE_BUILD_TYPE="$build_type" ..
    
    if [ $? -ne 0 ]; then
        print_error "CMake configuration failed"
        exit 1
    fi
    
    # Build
    print_status "Building..."
    make -j"$jobs"
    
    if [ $? -ne 0 ]; then
        print_error "Build failed"
        exit 1
    fi
    
    cd ..
    
    print_success "Build completed successfully!"
}

# Function to run tests
run_tests() {
    if [ ! -d "build" ]; then
        print_error "Build directory not found. Please build the project first."
        exit 1
    fi

    print_status "Running test suite (ctest)..."
    if ctest --test-dir build --output-on-failure; then
        print_success "Test suite passed"
    else
        print_error "Test suite failed"
        exit 1
    fi

    print_status "Running CLI smoke test..."
    if ./test_cli.sh build/bin/dte-cli; then
        print_success "All tests passed!"
    else
        print_error "CLI smoke test failed"
        exit 1
    fi
}

# Function to install
install_project() {
    print_status "Installing Device Tree Explorer..."
    
    if [ ! -d "build" ]; then
        print_error "Build directory not found. Please build the project first."
        exit 1
    fi
    
    cd build
    
    if [ "$EUID" -eq 0 ]; then
        make install
    else
        print_warning "Installing to user directory (requires sudo for system-wide install)"
        sudo make install
    fi
    
    cd ..
    
    print_success "Installation completed!"
}

# Function to clean build
clean_build() {
    print_status "Cleaning build directory..."
    
    if [ -d "build" ]; then
        rm -rf build
        print_success "Build directory cleaned"
    else
        print_warning "Build directory does not exist"
    fi
}

# Function to show help
show_help() {
    echo "Device Tree Explorer Build Script"
    echo
    echo "Usage: $0 [OPTION]"
    echo
    echo "Options:"
    echo "  build [type]     Build the project (default: Release)"
    echo "  debug            Build in debug mode"
    echo "  test             Run the test suite (ctest) and the CLI smoke test"
    echo "  install          Install the project"
    echo "  clean            Clean build directory"
    echo "  deps             Check dependencies"
    echo "  help             Show this help message"
    echo
    echo "Build types:"
    echo "  Release          Optimized release build (default)"
    echo "  Debug            Debug build with symbols"
    echo "  RelWithDebInfo   Release build with debug info"
    echo "  MinSizeRel       Minimum size release build"
    echo
    echo "Examples:"
    echo "  $0 build         # Build in release mode"
    echo "  $0 build Debug   # Build in debug mode"
    echo "  $0 test          # Run tests"
    echo "  $0 install       # Install the project"
}

# Main script logic
case "${1:-build}" in
    "build")
        check_dependencies
        build_project "${2:-Release}"
        ;;
    "debug")
        check_dependencies
        build_project "Debug"
        ;;
    "test")
        run_tests
        ;;
    "install")
        install_project
        ;;
    "clean")
        clean_build
        ;;
    "deps")
        check_dependencies
        ;;
    "help"|"-h"|"--help")
        show_help
        ;;
    *)
        print_error "Unknown option: $1"
        echo
        show_help
        exit 1
        ;;
esac 