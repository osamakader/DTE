# Device Tree Explorer

A modern C++ and Qt application for visualizing and editing Device Tree (DTB/DTS) files with both GUI and CLI interfaces.

## Features

### Core Functionality
- **Visualize and edit Device Tree files** - Load and display DTB/DTS files in a user-friendly tree structure
- **Diff two DTBs/DTS** - Compare device trees to see changes (overlays vs base)
- **Validate properties** - Check device tree validity and schema compliance (future feature)
- **Export parsed tree** - Export to JSON/YAML for automation and integration
- **CLI mode** - Full command-line interface for headless environments
- **Cross-platform** - Linux (primary), Windows/macOS (optional)

### GUI Features
- Modern Qt6-based interface
- Tree view for device tree nodes
- Property table for detailed property editing
- Source view for raw DTS content
- Diff viewer with visual change highlighting
- Drag and drop file support
- Search and filter capabilities

### CLI Features
- Colored output with ANSI support
- Comprehensive error handling and validation
- Multiple export formats (JSON, YAML, DTS, DTB)
- File conversion between DTB and DTS
- Search and list functionality
- Progress indicators and detailed feedback

## Installation

### Prerequisites

- **Linux**: Ubuntu 20.04+, Debian 11+, or similar
- **C++20 compatible compiler** (GCC 10+, Clang 12+)
- **CMake 3.16+**
- **Qt6** (Core, Widgets, Gui)
- **Device Tree Compiler (dtc)** - for validation and conversion

### Building from Source

```bash
# Clone the repository
git clone https://github.com/osamakader/DTE.git
cd DTE

# Create build directory
mkdir -p build && cd build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)

# Install (optional)
sudo make install
```

### Dependencies

#### Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools \
                 device-tree-compiler pkg-config
```

#### Fedora/RHEL
```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel device-tree-compiler pkg-config
```

#### Arch Linux
```bash
sudo pacman -S base-devel cmake qt6-base device-tree-compiler pkg-config
```

## Usage

### GUI Mode

Launch the graphical interface:
```bash
./bin/DeviceTreeExplorer
```

Or with command line options:
```bash
# Open a specific file
./bin/DeviceTreeExplorer -f device.dtb

# Compare with another file
./bin/DeviceTreeExplorer -f base.dtb -d overlay.dtb

# Export to JSON
./bin/DeviceTreeExplorer -f device.dtb -e json -o output.json
```

### CLI Mode

The CLI provides a comprehensive set of commands for device tree operations:

```bash
# Show help
./bin/dte-cli --help

# Get information about a device tree
./bin/dte-cli info device.dtb

# Validate a device tree
./bin/dte-cli validate device.dts

# Compare two device trees
./bin/dte-cli diff base.dtb overlay.dtb

# Export to different formats
./bin/dte-cli export device.dtb json output.json
./bin/dte-cli export device.dtb yaml output.yaml

# Convert between formats
./bin/dte-cli convert device.dtb device.dts
./bin/dte-cli convert device.dts device.dtb

# Search for nodes
./bin/dte-cli search device.dtb "cpu"

# List tree structure
./bin/dte-cli list device.dtb
./bin/dte-cli list device.dtb "/soc"
```

### CLI Commands Reference

#### `info <filename>`
Display detailed information about a device tree file.

**Output includes:**
- File size and format
- Total node and property counts
- Root node information
- File validation status

#### `validate <filename>`
Validate a device tree file for syntax and structure errors.

**Features:**
- Syntax checking
- Structure validation
- Property type verification
- Detailed error reporting

#### `diff <base_file> <overlay_file>`
Compare two device tree files and show differences.

**Output includes:**
- Added/removed/modified nodes
- Property changes
- Color-coded diff output
- Summary statistics

#### `export <input_file> <format> <output_file>`
Export device tree to different formats.

**Supported formats:**
- `json` - JSON format for automation
- `yaml` - YAML format for configuration
- `dts` - Device Tree Source format
- `dtb` - Device Tree Blob format

#### `convert <input_file> <output_file>`
Convert between DTB and DTS formats.

**Supported conversions:**
- DTB → DTS (decompilation)
- DTS → DTB (compilation)

#### `search <filename> <pattern>`
Search for nodes or properties matching a pattern.

**Features:**
- Pattern matching
- Recursive search
- Color-coded results
- Path display

#### `list <filename> [path]`
List the structure of a device tree.

**Features:**
- Tree visualization
- Property display
- Optional path filtering
- Hierarchical output

## Examples

### Basic Device Tree Analysis

```bash
# Get information about a device tree
./bin/dte-cli info /proc/device-tree

# Validate a custom device tree
./bin/dte-cli validate my-device.dts

# List the structure
./bin/dte-cli list my-device.dts
```

### Comparing Device Trees

```bash
# Compare base and overlay
./bin/dte-cli diff base.dtb overlay.dtb

# Compare different versions
./bin/dte-cli diff v1.0.dts v2.0.dts
```

### Export and Integration

```bash
# Export for automation
./bin/dte-cli export device.dtb json config.json

# Export for documentation
./bin/dte-cli export device.dtb yaml docs/device-tree.yaml

# Convert for editing
./bin/dte-cli convert device.dtb device.dts
```

### Search and Analysis

```bash
# Find all CPU nodes
./bin/dte-cli search device.dtb "cpu"

# Find memory-related properties
./bin/dte-cli search device.dtb "memory"

# List specific subtree
./bin/dte-cli list device.dtb "/soc/ethernet"
```

## Error Handling

The application provides comprehensive error handling:

### File Errors
- File not found
- Permission denied
- Invalid file format
- Corrupted data

### Parsing Errors
- Invalid DTB magic number
- Corrupted structure
- Invalid property values
- Version incompatibility

### Validation Errors
- Missing required properties
- Invalid property types
- Structure violations
- Schema compliance issues

### CLI Error Output
- Color-coded error messages
- Detailed error descriptions
- Suggested solutions
- Exit codes for automation

## Development

### Project Structure
```
DTE/
├── CMakeLists.txt          # Main build configuration
├── include/                # Public headers
│   ├── DeviceTree.h       # Core data structures
│   ├── DeviceTreeParser.h # Parser interfaces
│   ├── DeviceTreeDiff.h   # Diff functionality
│   ├── MainWindow.h       # GUI interface
│   └── CLIApp.h          # CLI interface
├── src/                   # GUI implementation
│   ├── CMakeLists.txt
│   ├── main.cpp          # GUI entry point
│   ├── DeviceTree.cpp    # Core implementation
│   ├── DeviceTreeParser.cpp # Parser implementation
│   ├── DeviceTreeDiff.cpp   # Diff implementation
│   ├── MainWindow.cpp    # GUI implementation
│   └── ...
├── cli/                   # CLI implementation
│   ├── CMakeLists.txt
│   ├── main.cpp          # CLI entry point
│   └── CLIApp.cpp        # CLI implementation
└── resources/            # Application resources
    ├── icons/            # Application icons
    └── translations/     # Localization files
```

### Building for Development

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build with optimizations
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### Testing

Unit and integration tests use GoogleTest and CTest. They are built
automatically when GoogleTest is installed (`libgtest-dev` on Debian/Ubuntu,
`gtest-devel` on Fedora, `gtest` on Arch). Integration tests that compile DTS
fixtures need `dtc` and are skipped without it.

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# With AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build-asan -DDTE_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j$(nproc) --target dte-tests
ctest --test-dir build-asan --output-on-failure
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

### Code Style
- Follow modern C++20 practices
- Use Qt6 conventions for GUI code
- Include proper error handling
- Add documentation for new features

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Built with Qt6 for cross-platform GUI
- Uses Device Tree Compiler (dtc) for validation
- Inspired by modern device tree tools
- Community contributions welcome

## Support

For issues and questions:
- Check the documentation
- Search existing issues
- Create a new issue with details
- Include error messages and file examples 