# Device Tree Explorer

A modern C++ and Qt application for visualizing and editing Device Tree (DTB/DTS) files with both GUI and CLI interfaces.

## Features

### Inputs
- DTB / DTBO blobs (detected by content), DTS / DTSI / DTSO sources (compiled
  with dtc, with C preprocessing when needed)
- The live device tree of a running Linux system: `/proc/device-tree`
  (`/sys/firmware/devicetree/base`), or `/sys/firmware/fdt` as root. In the
  GUI: File > Open System Device Tree. Property values are typed as for DTBs;
  directory order is not the original order, so nodes and properties are
  sorted by name.

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
- Colored output on terminals (plain when piped, or with `--no-color` / `NO_COLOR`)
- Comprehensive error handling and validation
- Export to JSON, YAML, DTS and DTB
- File conversion between DTB and DTS, including kernel-style sources with `#include`
- Search and list functionality
- Progress indicators and detailed feedback

## Installation

### Prerequisites

- **Linux**: Ubuntu 20.04+, Debian 11+, or similar
- **C++20 compatible compiler** (GCC 10+, Clang 12+)
- **CMake 3.16+**
- **Qt6** (Core, Widgets, Gui)
- **Device Tree Compiler (dtc)** - required at runtime to load DTS files and to convert between formats
- **C preprocessor (cpp)** - only for DTS files that use `#include`/`#define` (e.g. Linux kernel sources)

DTS files are compiled with `dtc` and then read with the built-in DTB parser,
so the full DTS language is supported and a DTS file loads exactly like its
compiled DTB. Files containing C preprocessor directives are first run through
`cpp -x assembler-with-cpp`, like the kernel build does; the file's directory
and its `include/` subdirectory are on the include path. Set `DTE_DTC` or
`DTE_CPP` to use a specific `dtc` or `cpp` binary.

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
# Open a specific file (-f is optional)
./bin/DeviceTreeExplorer device.dtb

# Compare with another file (opens the Diff tab)
./bin/DeviceTreeExplorer -f base.dtb -d overlay.dtb
```

In the GUI you can browse, search (Ctrl+F), edit nodes and properties with
undo/redo, follow phandle references, compare two trees, apply overlays
(Tools > Apply Overlay, undoable), validate (Tools > Validate, results in the
Problems tab) and save as DTS or DTB.

The GUI binary also works headless, without a display:
```bash
# Export or validate and exit (same output and exit codes as dte-cli)
./bin/DeviceTreeExplorer -f device.dts -e json -o output.json
./bin/DeviceTreeExplorer -f device.dts --validate

# Run any dte-cli command
./bin/DeviceTreeExplorer --cli diff base.dtb board.dts
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
Check a device tree against the Devicetree Specification's structural rules
(syntax errors are already reported when a DTS file is loaded).

**Checks:**
- Node and property names, duplicate sibling nodes
- Required root properties (`compatible`, `model`, `#address-cells`, `#size-cells`)
- `reg` length vs the parent's `#address-cells`/`#size-cells`, and unit
  addresses (`name@addr`) vs the first `reg` address
- Duplicate or reserved phandles, `interrupt-parent` targets
- `compatible` and `status` values, aliases pointing to existing nodes

Each issue is printed as `error:` or `warning:` with its node path. Exit
status is `0` when there are no errors (warnings are allowed), `1` otherwise.

#### `diff <base_file> <overlay_file>`
Compare two device tree files and show differences. Files may be any mix of
DTB and DTS; a DTS and its compiled DTB compare as identical.

**Output includes:**
- Added/removed/modified nodes and properties (an added or removed node also
  lists its properties)
- Old (`-`) and new (`+`) values in DTS syntax
- Color-coded diff output
- Summary statistics

**Exit status** (like `diff(1)`): `0` identical, `1` different, `2` error.

#### `export <input_file> <format> <output_file>`
Export device tree to different formats.

**Supported formats:**
- `json` - JSON format for automation
- `yaml` - YAML format for configuration
- `dts` - Device Tree Source (built-in writer, no dtc needed)
- `dtb` - Device Tree Blob (built-in writer, no dtc needed)

JSON and YAML use the same schema: source file, boot CPU id, memory reservations, and
nodes with `name`, `path`, `properties` and `children`, in tree order. String
properties become strings, string lists become arrays of strings, and cells and
bytes become arrays of numbers. Exporting a preprocessed source as `dts`
gives a flat, self-contained file with all `#include`s and macros resolved.

#### `convert <input_file> <output_file>`
Convert between DTB and DTS formats.

**Supported conversions:**
- DTB → DTS (decompilation)
- DTS → DTB (compilation, with C preprocessing when the source uses `#include`/`#define`)

The input format is detected from the file contents; the output format from
the extension (`.dtb`/`.dtbo` or `.dts`/`.dtsi`/`.dtso`).

#### Global options
- `-I DIR`, `--include DIR` - add an include directory for DTS sources (repeatable)
- `--no-color` - disable colored output (`NO_COLOR=1` also works)

Status messages (`INFO:`, `ERROR:`, ...) go to stderr, so stdout can be piped.

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

#### `refs <filename> [path]`
Show phandle references. Without a path, lists every reference in the tree
(`source:property -> target <argument cells>`), with broken ones explained.
With a path, shows what that node refers to and which nodes refer to it.

References are decoded from the properties that hold phandles
(`interrupt-parent`, `clocks`, `gpios`/`*-gpios`, `resets`, `dmas`,
`interrupts-extended`, `pinctrl-N`, ...) using the target's `#...-cells`.
`validate` reports references that do not resolve. In the GUI, the property
table's "Refers to" column and the "Referenced by" list show the same
information and let you jump between nodes.

#### `overlay <base> <output> <overlay>...`
Apply device tree overlays to a base tree, like `fdtoverlay`, and save the
result as DTB or DTS (by extension). Overlays may be compiled `.dtbo` files or
`.dtso` sources with `/plugin/;`; the base may be a DTB or a DTS file.

Overlays refer to base labels (`&uart0`) through the base's `__symbols__`
node, which `dtc` only emits with `-@`. For a DTS base the labels are taken
from a separate `dtc -@` compile, so the base does not need to be rebuilt, and
labelled nodes that have no phandle yet get one. Labels defined by one overlay
can be used by the next.

```bash
./bin/dte-cli overlay board.dtb merged.dtb uart.dtbo sensor.dtbo
./bin/dte-cli overlay board.dts merged.dts uart.dtso
./bin/dte-cli diff board.dts merged.dts   # what the overlays changed
```

#### `memmap <filename>`
Show the CPU memory map: every `reg` entry translated through the parent
buses' `ranges` (like the kernel's `of_translate_address`), sorted by
address, with overlapping regions flagged. Entries that cannot be translated,
such as devices on I2C/SPI buses or addresses outside a bus's `ranges`, are
listed separately with the reason. The GUI shows the same decoding in the
property table's Details column, and `validate` warns about addresses outside
`ranges`. Addresses up to 64 bits are supported (not 3-cell PCI addresses).

#### `irqs <filename>`
List all interrupts grouped by the interrupt controller they end up at,
following the kernel's resolution: `interrupt-parent` (inherited, or the
nearest ancestor with `#interrupt-cells`), `interrupts-extended`, and
`interrupt-map` / `interrupt-map-mask` nexus nodes. ARM GIC specifiers are
explained (`SPI 33 (hwirq 65), level high`), two-cell specifiers as line and
trigger, and `interrupt-names` label the entries. Shared lines and interrupts
that do not resolve are flagged. The GUI's Details column shows the same for
`interrupts` properties, and `validate` reports unresolvable interrupts.

## Examples

### Basic Device Tree Analysis

```bash
# Get information about a device tree
./bin/dte-cli info device.dtb

# Inspect the running system's device tree (Linux, e.g. on the board)
./bin/dte-cli list /proc/device-tree
./bin/dte-cli export /proc/device-tree dts live.dts
./bin/dte-cli diff my-board.dts /proc/device-tree   # what the kernel actually got

# Load a kernel-style DTS that includes dt-bindings headers
./bin/dte-cli -I linux/include list linux/arch/arm64/boot/dts/vendor/board.dts

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