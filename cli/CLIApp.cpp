#include "CLIApp.h"
#include "Overlay.h"
#include "Addresses.h"
#include "Interrupts.h"
#include "Escape.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unistd.h>
#include <filesystem>
#include <set>

namespace DTE {

// ANSI color codes. Cleared by disableColors() when output is not a
// terminal or NO_COLOR is set (https://no-color.org).
namespace Colors {
    std::string RESET = "\033[0m";
    std::string RED = "\033[31m";
    std::string GREEN = "\033[32m";
    std::string YELLOW = "\033[33m";
    std::string BLUE = "\033[34m";
    std::string MAGENTA = "\033[35m";
    std::string CYAN = "\033[36m";
    std::string WHITE = "\033[37m";
    std::string BOLD = "\033[1m";
    std::string UNDERLINE = "\033[4m";

    void disableColors() {
        for (auto* c : {&RESET, &RED, &GREEN, &YELLOW, &BLUE, &MAGENTA, &CYAN, &WHITE, &BOLD, &UNDERLINE}) {
            c->clear();
        }
    }
}

// Utility functions
namespace {
    bool shouldUseColor() {
        const char* noColor = std::getenv("NO_COLOR");
        if (noColor && *noColor) return false;
        return isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
    }
    
    std::string formatBytes(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit = 0;
        double size = static_cast<double>(bytes);
        
        while (size >= 1024.0 && unit < 3) {
            size /= 1024.0;
            unit++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << size << " " << units[unit];
        return oss.str();
    }
    
    std::string getFileExtension(const std::string& filename) {
        size_t pos = filename.find_last_of('.');
        if (pos != std::string::npos) {
            return filename.substr(pos + 1);
        }
        return "";
    }
    
    // Exists (file or directory, e.g. /proc/device-tree); whether it can
    // be read is reported when loading.
    bool fileExists(const std::string& filename) {
        std::error_code ec;
        return std::filesystem::exists(filename, ec);
    }
}

CLIApp::CLIApp(int argc, char* argv[])
    : programName_(argv[0]) {
    
    // Convert argv to vector of strings
    for (int i = 1; i < argc; ++i) {
        args_.push_back(argv[i]);
    }
    
    if (!shouldUseColor()) {
        Colors::disableColors();
    }

    setupCommands();
}

bool CLIApp::parseGlobalOptions() {
    std::vector<std::string> rest;
    for (size_t i = 0; i < args_.size(); ++i) {
        const std::string& a = args_[i];
        if (a == "--") {
            rest.insert(rest.end(), args_.begin() + i + 1, args_.end());
            break;
        }
        if (a == "-I" || a == "--include") {
            if (i + 1 >= args_.size()) {
                printError("Option " + a + " requires a directory argument");
                return false;
            }
            includePaths_.push_back(args_[++i]);
        } else if (a.rfind("--include=", 0) == 0) {
            includePaths_.push_back(a.substr(10));
        } else if (a.size() > 2 && a.rfind("-I", 0) == 0) {
            includePaths_.push_back(a.substr(2));
        } else if (a == "--no-color") {
            Colors::disableColors();
        } else if (a == "--json") {
            json_ = true;
        } else {
            rest.push_back(a);
        }
    }
    args_ = std::move(rest);
    return true;
}

int CLIApp::run() {
    if (!parseGlobalOptions()) {
        return EXIT_FAILURE;
    }

    if (args_.empty()) {
        printUsage();
        return EXIT_FAILURE;
    }
    
    std::string command = args_[0];
    
    // Handle special commands
    if (command == "--version" || command == "-v") {
        printVersion();
        return EXIT_SUCCESS;
    }
    
    if (command == "--help" || command == "-h") {
        printHelp();
        return EXIT_SUCCESS;
    }
    
    // Find and execute command
    auto it = commands_.find(command);
    if (it == commands_.end()) {
        printError("Unknown command: " + command);
        printUsage();
        return EXIT_FAILURE;
    }
    
    // Commands that can print JSON
    static const std::set<std::string> jsonCommands = {"get", "info", "validate", "diff", "refs", "memmap", "irqs"};
    if (json_ && !jsonCommands.count(command)) {
        printError("--json is not supported by '" + command + "'");
        return EXIT_FAILURE;
    }

    // Extract command arguments
    std::vector<std::string> cmdArgs(args_.begin() + 1, args_.end());
    
    try {
        return it->second.handler(cmdArgs);
    } catch (const std::exception& e) {
        printError("Command failed: " + std::string(e.what()));
        return EXIT_FAILURE;
    }
}

void CLIApp::setupCommands() {
    CLICommand infoCmd;
    infoCmd.name = "info";
    infoCmd.description = "Display information about a device tree file";
    infoCmd.usage = "info <filename>";
    infoCmd.handler = [this](const std::vector<std::string>& args) { return handleInfo(args); };
    commands_["info"] = infoCmd;
    
    CLICommand validateCmd;
    validateCmd.name = "validate";
    validateCmd.description = "Validate a device tree file";
    validateCmd.usage = "validate <filename>";
    validateCmd.handler = [this](const std::vector<std::string>& args) { return handleValidate(args); };
    commands_["validate"] = validateCmd;
    
    CLICommand diffCmd;
    diffCmd.name = "diff";
    diffCmd.description = "Compare two device tree files";
    diffCmd.usage = "diff <base_file> <overlay_file>";
    diffCmd.handler = [this](const std::vector<std::string>& args) { return handleDiff(args); };
    commands_["diff"] = diffCmd;
    
    CLICommand exportCmd;
    exportCmd.name = "export";
    exportCmd.description = "Export device tree to different format";
    exportCmd.usage = "export <input_file> <format> <output_file>";
    exportCmd.handler = [this](const std::vector<std::string>& args) { return handleExport(args); };
    commands_["export"] = exportCmd;
    
    CLICommand convertCmd;
    convertCmd.name = "convert";
    convertCmd.description = "Convert between DTB and DTS formats";
    convertCmd.usage = "convert <input_file> <output_file>";
    convertCmd.handler = [this](const std::vector<std::string>& args) { return handleConvert(args); };
    commands_["convert"] = convertCmd;
    
    CLICommand searchCmd;
    searchCmd.name = "search";
    searchCmd.description = "Search for nodes or properties in device tree";
    searchCmd.usage = "search <filename> <pattern>";
    searchCmd.handler = [this](const std::vector<std::string>& args) { return handleSearch(args); };
    commands_["search"] = searchCmd;
    
    CLICommand listCmd;
    listCmd.name = "list";
    listCmd.description = "List nodes and properties in device tree";
    listCmd.usage = "list <filename> [path]";
    listCmd.handler = [this](const std::vector<std::string>& args) { return handleList(args); };
    commands_["list"] = listCmd;

    CLICommand refsCmd;
    refsCmd.name = "refs";
    refsCmd.description = "Show phandle references (all, or from/to one node)";
    refsCmd.usage = "refs <filename> [path]";
    refsCmd.handler = [this](const std::vector<std::string>& args) { return handleRefs(args); };
    commands_["refs"] = refsCmd;

    CLICommand overlayCmd;
    overlayCmd.name = "overlay";
    overlayCmd.description = "Apply overlays (.dtbo/.dtso) to a base tree and save the result";
    overlayCmd.usage = "overlay <base> <output> <overlay>...";
    overlayCmd.handler = [this](const std::vector<std::string>& args) { return handleOverlay(args); };
    commands_["overlay"] = overlayCmd;

    CLICommand memmapCmd;
    memmapCmd.name = "memmap";
    memmapCmd.description = "Show the CPU memory map of all reg regions, with overlaps";
    memmapCmd.usage = "memmap <filename>";
    memmapCmd.handler = [this](const std::vector<std::string>& args) { return handleMemmap(args); };
    commands_["memmap"] = memmapCmd;

    CLICommand irqsCmd;
    irqsCmd.name = "irqs";
    irqsCmd.description = "List interrupts by controller, resolved through interrupt-map";
    irqsCmd.usage = "irqs <filename>";
    irqsCmd.handler = [this](const std::vector<std::string>& args) { return handleIrqs(args); };
    commands_["irqs"] = irqsCmd;

    CLICommand getCmd;
    getCmd.name = "get";
    getCmd.description = "Print a property value, or a node's properties and children";
    getCmd.usage = "get <filename> <path|alias> [property]";
    getCmd.handler = [this](const std::vector<std::string>& args) { return handleGet(args); };
    commands_["get"] = getCmd;
    
    CLICommand helpCmd;
    helpCmd.name = "help";
    helpCmd.description = "Show help for a command";
    helpCmd.usage = "help [command]";
    helpCmd.handler = [this](const std::vector<std::string>& args) { return handleHelp(args); };
    commands_["help"] = helpCmd;
}

void CLIApp::printUsage() const {
    std::cout << Colors::BOLD << "Device Tree Explorer CLI" << Colors::RESET << "\n\n";
    std::cout << "Usage: " << programName_ << " [global options] <command> [arguments]\n\n";
    std::cout << Colors::BOLD << "Available commands:" << Colors::RESET << "\n";
    
    int maxWidth = 0;
    for (const auto& cmd : commands_) {
        maxWidth = std::max(maxWidth, static_cast<int>(cmd.second.name.length()));
    }
    
    for (const auto& cmd : commands_) {
        std::cout << "  " << std::setw(maxWidth) << std::left << cmd.second.name 
                  << " " << cmd.second.description << "\n";
    }
    
    std::cout << "\n" << Colors::BOLD << "Global options:" << Colors::RESET << "\n";
    std::cout << "  -I, --include DIR  Add an include directory for DTS sources (repeatable)\n";
    std::cout << "  --json             JSON output (get, info, validate, diff, refs, memmap, irqs)\n";
    std::cout << "  --no-color         Disable colored output (also: NO_COLOR=1)\n";
    std::cout << "\nUse '" << programName_ << " help <command>' for detailed help.\n";
    std::cout << "Use '" << programName_ << " --version' to show version information.\n";
}

void CLIApp::printVersion() const {
    std::cout << Colors::BOLD << "Device Tree Explorer CLI v1.0.0" << Colors::RESET << "\n";
    std::cout << "Copyright (c) 2024 DTE Project\n";
    std::cout << "Built with modern C++ and Qt6\n";
}

void CLIApp::printHelp(const std::string& command) const {
    if (command.empty()) {
        printUsage();
        return;
    }
    
    auto it = commands_.find(command);
    if (it == commands_.end()) {
        printError("Unknown command: " + command);
        return;
    }
    
    const auto& cmd = it->second;
    std::cout << Colors::BOLD << "Command: " << Colors::RESET << cmd.name << "\n";
    std::cout << Colors::BOLD << "Description: " << Colors::RESET << cmd.description << "\n";
    std::cout << Colors::BOLD << "Usage: " << Colors::RESET << programName_ << " " << cmd.usage << "\n";
    
    // Add specific help for each command
    if (command == "info") {
        std::cout << "\n" << Colors::BOLD << "Examples:" << Colors::RESET << "\n";
        std::cout << "  " << programName_ << " info device.dtb\n";
        std::cout << "  " << programName_ << " -I include info board.dts\n";
    } else if (command == "diff") {
        std::cout << "\n" << Colors::BOLD << "Examples:" << Colors::RESET << "\n";
        std::cout << "  " << programName_ << " diff base.dtb overlay.dtb\n";
        std::cout << "  " << programName_ << " diff original.dts modified.dts\n";
        std::cout << "\n" << Colors::BOLD << "Exit status:" << Colors::RESET << "\n";
        std::cout << "  0 if the trees are identical, 1 if they differ, 2 on error\n";
    } else if (command == "export") {
        std::cout << "\n" << Colors::BOLD << "Supported formats:" << Colors::RESET << "\n";
        std::cout << "  json    - JSON format\n";
        std::cout << "  yaml    - YAML format\n";
        std::cout << "  dts     - Device Tree Source (built-in writer, no dtc needed)\n";
        std::cout << "  dtb     - Device Tree Blob (built-in writer, no dtc needed)\n";
        std::cout << "\n'" << programName_ << " convert' instead uses dtc, which keeps dtc's own\n";
        std::cout << "formatting (labels, value grouping) when decompiling.\n";
    } else if (command == "refs") {
        std::cout << "\nReferences are decoded from properties that hold phandles (interrupt-parent,\n";
        std::cout << "clocks, gpios and *-gpios, resets, dmas, interrupts-extended, pinctrl-N, ...),\n";
        std::cout << "using the target's #...-cells for the argument cells.\n";
        std::cout << "\n" << Colors::BOLD << "Examples:" << Colors::RESET << "\n";
        std::cout << "  " << programName_ << " refs board.dtb\n";
        std::cout << "  " << programName_ << " refs board.dtb /soc/interrupt-controller@1000\n";
    } else if (command == "overlay") {
        std::cout << "\nOverlays are applied in order, like fdtoverlay. The output format follows its\n";
        std::cout << "extension (.dtb/.dtbo or .dts). The base may be DTS: labels used by the\n";
        std::cout << "overlays are then taken from a `dtc -@` compile of it. Overlay sources\n";
        std::cout << "(.dtso) need /plugin/;.\n";
        std::cout << "\n" << Colors::BOLD << "Examples:" << Colors::RESET << "\n";
        std::cout << "  " << programName_ << " overlay board.dtb merged.dtb uart.dtbo sensor.dtbo\n";
        std::cout << "  " << programName_ << " overlay board.dts merged.dts uart.dtso\n";
    } else if (command == "memmap") {
        std::cout << "\nEvery reg entry is translated to a CPU address through the parent buses'\n";
        std::cout << "ranges (like the kernel's of_translate_address) and listed by address.\n";
        std::cout << "Overlapping regions are flagged. Entries that cannot be translated (e.g.\n";
        std::cout << "devices on I2C/SPI buses) are listed separately with the reason.\n";
    } else if (command == "get") {
        std::cout << "\nWith a property, prints its value in DTS syntax (nothing for an empty\n";
        std::cout << "property); without, lists the node's properties and its children (name/).\n";
        std::cout << "The path may also be an alias name. Exit status 1 if not found.\n";
        std::cout << "With --json the value uses the export schema's mapping.\n";
        std::cout << "\n" << Colors::BOLD << "Examples:" << Colors::RESET << "\n";
        std::cout << "  " << programName_ << " get board.dtb / model\n";
        std::cout << "  " << programName_ << " get board.dtb serial0 status\n";
        std::cout << "  " << programName_ << " --json get board.dtb /soc/serial@10000000\n";
    } else if (command == "convert") {
        std::cout << "\nThe input format is detected from the file contents; the output\n";
        std::cout << "format from the extension (.dtb/.dtbo or .dts/.dtsi/.dtso).\n";
        std::cout << "\n" << Colors::BOLD << "Examples:" << Colors::RESET << "\n";
        std::cout << "  " << programName_ << " convert board.dtb board.dts\n";
        std::cout << "  " << programName_ << " -I include convert board.dts board.dtb\n";
    }
}

int CLIApp::handleInfo(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        printError("Usage: " + commands_["info"].usage);
        return EXIT_FAILURE;
    }
    
    std::string filename = args[0];
    
    // Check if file exists
    if (!fileExists(filename)) {
        printError("File not found: " + filename);
        return EXIT_FAILURE;
    }
    
    printInfo("Loading device tree from: " + filename);
    
    if (!loadDeviceTree(filename)) {
        return EXIT_FAILURE;
    }
    
    printDeviceTreeInfo();
    return EXIT_SUCCESS;
}

int CLIApp::handleValidate(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        printError("Usage: " + commands_["validate"].usage);
        return EXIT_FAILURE;
    }
    
    std::string filename = args[0];
    
    if (!fileExists(filename)) {
        printError("File not found: " + filename);
        return EXIT_FAILURE;
    }
    
    printInfo("Validating device tree: " + filename);
    
    if (!loadDeviceTree(filename)) {
        return EXIT_FAILURE;
    }
    
    return printValidationResults() ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Exit status follows diff(1): 0 = identical, 1 = different, 2 = error.
constexpr int DIFF_EXIT_SAME = 0;
constexpr int DIFF_EXIT_DIFFERENT = 1;
constexpr int DIFF_EXIT_ERROR = 2;

int CLIApp::handleDiff(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        printError("Usage: " + commands_["diff"].usage);
        return DIFF_EXIT_ERROR;
    }
    
    std::string baseFile = args[0];
    std::string overlayFile = args[1];
    
    if (!fileExists(baseFile)) {
        printError("Base file not found: " + baseFile);
        return DIFF_EXIT_ERROR;
    }
    
    if (!fileExists(overlayFile)) {
        printError("Overlay file not found: " + overlayFile);
        return DIFF_EXIT_ERROR;
    }
    
    printInfo("Loading base device tree: " + baseFile);
    auto baseTree = std::make_shared<DeviceTree>();
    if (!baseTree->loadFromFile(baseFile, loadOptions())) {
        printError("Failed to load base file: " + baseFile + "\n" + baseTree->getLoadError());
        return DIFF_EXIT_ERROR;
    }
    
    printInfo("Loading overlay device tree: " + overlayFile);
    auto overlayTree = std::make_shared<DeviceTree>();
    if (!overlayTree->loadFromFile(overlayFile, loadOptions())) {
        printError("Failed to load overlay file: " + overlayFile + "\n" + overlayTree->getLoadError());
        return DIFF_EXIT_ERROR;
    }
    
    printInfo("Generating diff...");
    DeviceTreeDiff diff(baseTree, overlayTree);
    if (json_) {
        std::cout << diff.exportAsJSON();
    } else {
        printDiffResults(diff);
    }
    
    return diff.getTotalChanges() > 0 ? DIFF_EXIT_DIFFERENT : DIFF_EXIT_SAME;
}

int CLIApp::handleExport(const std::vector<std::string>& args) {
    if (args.size() != 3) {
        printError("Usage: " + commands_["export"].usage);
        return EXIT_FAILURE;
    }
    
    std::string inputFile = args[0];
    std::string format = args[1];
    std::string outputFile = args[2];
    
    if (!fileExists(inputFile)) {
        printError("Input file not found: " + inputFile);
        return EXIT_FAILURE;
    }
    
    if (format != "json" && format != "yaml" && format != "dts" && format != "dtb") {
        printError("Unsupported format: " + format);
        printInfo("Supported formats: json, yaml, dts, dtb");
        return EXIT_FAILURE;
    }

    printInfo("Loading device tree from: " + inputFile);
    if (!loadDeviceTree(inputFile)) {
        return EXIT_FAILURE;
    }
    
    printInfo("Exporting to " + format + " format: " + outputFile);

    if (format == "dts" || format == "dtb") {
        std::string error;
        if (!deviceTree_->saveToFile(outputFile, format == "dts", &error)) {
            printError(error);
            return EXIT_FAILURE;
        }
    } else {
        std::ofstream outFile(outputFile, std::ios::binary | std::ios::trunc);
        if (!outFile) {
            printError("Failed to open output file: " + outputFile);
            return EXIT_FAILURE;
        }
        outFile << (format == "json" ? deviceTree_->exportAsJSON() : deviceTree_->exportAsYAML());
        if (!outFile.flush()) {
            printError("Failed to write output file: " + outputFile);
            return EXIT_FAILURE;
        }
    }
    printSuccess("Successfully exported to: " + outputFile);
    
    return EXIT_SUCCESS;
}

int CLIApp::handleConvert(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        printError("Usage: " + commands_["convert"].usage);
        return EXIT_FAILURE;
    }
    
    std::string inputFile = args[0];
    std::string outputFile = args[1];
    
    if (!fileExists(inputFile)) {
        printError("Input file not found: " + inputFile);
        return EXIT_FAILURE;
    }
    
    auto parser = DeviceTreeParserFactory::createParser(inputFile);
    const bool inputIsDtb = dynamic_cast<DTBParser*>(parser.get()) != nullptr;
    const bool inputIsDts = dynamic_cast<DTSParser*>(parser.get()) != nullptr;
    const std::string outputExt = getFileExtension(outputFile);
    const bool outputIsDtb = outputExt == "dtb" || outputExt == "dtbo";
    const bool outputIsDts = outputExt == "dts" || outputExt == "dtsi" || outputExt == "dtso";

    std::string error;
    bool success = false;
    if (inputIsDtb && outputIsDts) {
        printInfo("Decompiling DTB to DTS: " + inputFile + " -> " + outputFile);
        success = DeviceTreeUtils::dtbToDts(inputFile, outputFile, &error);
    } else if (inputIsDts && outputIsDtb) {
        printInfo("Compiling DTS to DTB: " + inputFile + " -> " + outputFile);
        success = DeviceTreeUtils::dtsToDtb(inputFile, outputFile, includePaths_, &error);
    } else {
        printError("Unsupported conversion: " + inputFile + " -> " + outputFile);
        printInfo("Supported conversions: DTB -> .dts/.dtsi/.dtso, DTS -> .dtb/.dtbo");
        return EXIT_FAILURE;
    }
    
    if (!success) {
        printError("Conversion failed\n" + error);
        return EXIT_FAILURE;
    }
    printSuccess("Successfully converted: " + inputFile + " -> " + outputFile);
    return EXIT_SUCCESS;
}

int CLIApp::handleSearch(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        printError("Usage: " + commands_["search"].usage);
        return EXIT_FAILURE;
    }
    
    std::string filename = args[0];
    std::string pattern = args[1];
    
    if (!fileExists(filename)) {
        printError("File not found: " + filename);
        return EXIT_FAILURE;
    }
    
    printInfo("Searching for pattern '" + pattern + "' in: " + filename);
    
    if (!loadDeviceTree(filename)) {
        return EXIT_FAILURE;
    }
    
    auto nodes = deviceTree_->findNodesByPattern(pattern);
    
    std::cout << Colors::BOLD << "Search Results:" << Colors::RESET << "\n";
    std::cout << "Found " << Colors::CYAN << nodes.size() << Colors::RESET << " nodes matching '" << pattern << "':\n";
    
    if (nodes.empty()) {
        printWarning("No nodes found matching the pattern");
    } else {
        for (const auto& node : nodes) {
            std::cout << "  " << Colors::GREEN << node->getFullPath() << Colors::RESET << "\n";
        }
    }
    
    return nodes.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
}

int CLIApp::handleList(const std::vector<std::string>& args) {
    if (args.size() < 1 || args.size() > 2) {
        printError("Usage: " + commands_["list"].usage);
        return EXIT_FAILURE;
    }
    
    std::string filename = args[0];
    
    if (!fileExists(filename)) {
        printError("File not found: " + filename);
        return EXIT_FAILURE;
    }
    
    printInfo("Loading device tree from: " + filename);
    if (!loadDeviceTree(filename)) {
        return EXIT_FAILURE;
    }
    
    std::shared_ptr<DeviceTreeNode> targetNode = deviceTree_->getRoot();
    if (args.size() == 2) {
        targetNode = deviceTree_->findNodeByPath(args[1]);
        if (!targetNode) {
            printError("Node not found: " + args[1]);
            return EXIT_FAILURE;
        }
    }
    
    std::cout << Colors::BOLD << "Device Tree Structure:" << Colors::RESET << "\n";
    printTree(targetNode);
    return EXIT_SUCCESS;
}

int CLIApp::handleHelp(const std::vector<std::string>& args) {
    std::string command = args.empty() ? "" : args[0];
    printHelp(command);
    return EXIT_SUCCESS;
}

bool CLIApp::loadDeviceTree(const std::string& filename) {
    deviceTree_ = std::make_shared<DeviceTree>();
    if (!deviceTree_->loadFromFile(filename, loadOptions())) {
        printError("Failed to load device tree file: " + filename + "\n" + deviceTree_->getLoadError());
        return false;
    }
    return true;
}

void CLIApp::printDeviceTreeInfo() const {
    if (!deviceTree_) return;

    if (json_) {
        size_t nodes = 0, properties = 0;
        std::function<void(const std::shared_ptr<DeviceTreeNode>&)> count = [&](const std::shared_ptr<DeviceTreeNode>& n) {
            ++nodes;
            properties += n->getPropertyCount();
            for (const auto& c : n->getChildren()) count(c);
        };
        count(deviceTree_->getRoot());
        std::error_code ec;
        const auto& file = deviceTree_->getSourceFile();
        const bool regular = std::filesystem::is_regular_file(file, ec);
        std::cout << "{\"file\": " << detail::jsonQuote(file) << ", \"nodes\": " << nodes
                  << ", \"properties\": " << properties
                  << ", \"memory_reservations\": " << deviceTree_->getMemoryReservations().size()
                  << ", \"file_size\": ";
        if (regular) std::cout << std::filesystem::file_size(file, ec); else std::cout << "null";
        std::cout << "}\n";
        return;
    }
    
    std::cout << Colors::BOLD << "Device Tree Information:" << Colors::RESET << "\n";
    std::cout << "Source file: " << Colors::CYAN << deviceTree_->getSourceFile() << Colors::RESET << "\n";
    std::cout << "Root node: " << Colors::GREEN << deviceTree_->getRoot()->getName() << Colors::RESET << "\n";
    
    // Count nodes and properties
    size_t nodeCount = 0;
    size_t propertyCount = 0;
    
    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> countNodes = 
        [&](const std::shared_ptr<DeviceTreeNode>& node) {
            nodeCount++;
            propertyCount += node->getPropertyCount();
            for (const auto& child : node->getChildren()) {
                countNodes(child);
            }
        };
    
    countNodes(deviceTree_->getRoot());
    
    std::cout << "Total nodes: " << Colors::YELLOW << nodeCount << Colors::RESET << "\n";
    std::cout << "Total properties: " << Colors::YELLOW << propertyCount << Colors::RESET << "\n";
    
    // File information
    std::ifstream file(deviceTree_->getSourceFile());
    if (file) {
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        std::cout << "File size: " << Colors::BLUE << formatBytes(fileSize) << Colors::RESET << "\n";
    }
}

bool CLIApp::printValidationResults() const {
    if (!deviceTree_) return false;

    const auto issues = deviceTree_->check();
    if (json_) {
        using detail::jsonQuote;
        size_t errors = 0;
        for (const auto& i : issues) errors += i.isError();
        std::cout << "{\"valid\": " << (errors == 0 ? "true" : "false") << ", \"errors\": " << errors
                  << ", \"warnings\": " << issues.size() - errors << ", \"issues\": [";
        for (size_t k = 0; k < issues.size(); ++k) {
            const auto& i = issues[k];
            std::cout << (k ? ", " : "") << "{\"severity\": " << jsonQuote(i.isError() ? "error" : "warning")
                      << ", \"path\": " << jsonQuote(i.path) << ", \"property\": "
                      << (i.property.empty() ? "null" : jsonQuote(i.property)) << ", \"message\": "
                      << jsonQuote(i.message) << "}";
        }
        std::cout << "]}\n";
        return errors == 0;
    }
    size_t errors = 0;
    for (const auto& issue : issues) {
        if (issue.isError()) ++errors;
        const std::string& color = issue.isError() ? Colors::RED : Colors::YELLOW;
        std::cout << color << Colors::BOLD << (issue.isError() ? "error" : "warning") << Colors::RESET
                  << ": " << issue.path;
        if (!issue.property.empty()) std::cout << ":" << Colors::CYAN << issue.property << Colors::RESET;
        std::cout << ": " << issue.message << "\n";
    }

    const size_t warnings = issues.size() - errors;
    if (errors == 0 && warnings == 0) {
        printSuccess("No problems found");
    } else if (errors == 0) {
        printSuccess("Valid, with " + std::to_string(warnings) + " warning(s)");
    } else {
        printError(std::to_string(errors) + " error(s), " + std::to_string(warnings) + " warning(s)");
    }
    return errors == 0;
}

void CLIApp::printDiffResults(const DeviceTreeDiff& diff) const {
    std::cout << Colors::BOLD << "Diff Results:" << Colors::RESET << "\n";
    
    auto stats = diff.getTotalChanges();
    std::cout << "Total changes: " << Colors::YELLOW << stats << Colors::RESET << "\n";
    std::cout << "Added: " << Colors::GREEN << diff.getAddedCount() << Colors::RESET << "\n";
    std::cout << "Removed: " << Colors::RED << diff.getRemovedCount() << Colors::RESET << "\n";
    std::cout << "Modified: " << Colors::YELLOW << diff.getModifiedCount() << Colors::RESET << "\n";
    
    auto changes = diff.generateDiff();
    if (!changes.empty()) {
        std::cout << "\n" << Colors::BOLD << "Detailed Changes:" << Colors::RESET << "\n";
        for (const auto& change : changes) {
            std::string typeStr;
            std::string colorCode;
            
            switch (change.type) {
                case DiffType::Added: 
                    typeStr = "[ADD]"; 
                    colorCode = Colors::GREEN;
                    break;
                case DiffType::Removed: 
                    typeStr = "[DEL]"; 
                    colorCode = Colors::RED;
                    break;
                case DiffType::Modified: 
                    typeStr = "[MOD]"; 
                    colorCode = Colors::YELLOW;
                    break;
                default: 
                    typeStr = "[UNK]"; 
                    colorCode = Colors::RESET;
                    break;
            }
            
            std::cout << "  " << colorCode << typeStr << Colors::RESET << " " << change.path;
            if (!change.propertyName.empty()) {
                std::cout << ":" << Colors::CYAN << change.propertyName << Colors::RESET;
            }
            std::cout << " - " << change.description << "\n";
            if (change.type == DiffType::Modified || change.type == DiffType::Removed) {
                if (!change.propertyName.empty()) {
                    std::cout << "      " << Colors::RED << "- " << change.oldValue << Colors::RESET << "\n";
                }
            }
            if (change.type == DiffType::Modified || change.type == DiffType::Added) {
                if (!change.propertyName.empty()) {
                    std::cout << "      " << Colors::GREEN << "+ " << change.newValue << Colors::RESET << "\n";
                }
            }
        }
    }
}

void CLIApp::printTree(const std::shared_ptr<DeviceTreeNode>& node, const std::string& prefix) const {
    if (!node) return;
    
    std::cout << prefix << Colors::GREEN << node->getName() << Colors::RESET << "\n";
    
    // Print properties
    auto properties = node->getProperties();
    for (const auto& prop : properties) {
        std::cout << prefix << "  " << Colors::CYAN << prop.getName() << Colors::RESET;
        if (!prop.isEmpty()) {
            std::cout << " = " << Colors::YELLOW << prop.getValueAsString() << Colors::RESET;
        }
        std::cout << "\n";
    }
    
    // Print children
    for (const auto& child : node->getChildren()) {
        printTree(child, prefix + "  ");
    }
}

// Status messages go to stderr so stdout carries only command output.
void CLIApp::printError(const std::string& message) const {
    std::cerr << Colors::RED << Colors::BOLD << "ERROR: " << Colors::RESET << message << "\n";
}

void CLIApp::printWarning(const std::string& message) const {
    std::cerr << Colors::YELLOW << Colors::BOLD << "WARNING: " << Colors::RESET << message << "\n";
}

void CLIApp::printSuccess(const std::string& message) const {
    std::cerr << Colors::GREEN << Colors::BOLD << "SUCCESS: " << Colors::RESET << message << "\n";
}

void CLIApp::printInfo(const std::string& message) const {
    std::cerr << Colors::BLUE << Colors::BOLD << "INFO: " << Colors::RESET << message << "\n";
}

} // namespace DTE 
namespace DTE {

namespace {

std::string formatReference(const PhandleReference& r, bool withSource) {
    std::ostringstream os;
    if (withSource) os << r.source->getFullPath() << ":";
    os << r.property << " -> ";
    if (!r.problem.empty()) {
        os << "(" << r.problem << ")";
        return os.str();
    }
    os << r.target->getFullPath();
    if (!r.args.empty()) {
        os << " <" << std::hex;
        for (size_t i = 0; i < r.args.size(); ++i) os << (i ? " " : "") << "0x" << r.args[i];
        os << ">" << std::dec;
    }
    return os.str();
}

} // namespace

int CLIApp::handleRefs(const std::vector<std::string>& args) {
    if (args.empty() || args.size() > 2) {
        printError("Usage: " + commands_["refs"].usage);
        return EXIT_FAILURE;
    }
    if (!fileExists(args[0])) {
        printError("File not found: " + args[0]);
        return EXIT_FAILURE;
    }
    printInfo("Loading device tree from: " + args[0]);
    if (!loadDeviceTree(args[0])) {
        return EXIT_FAILURE;
    }

    const auto refs = deviceTree_->references();
    if (json_) {
        using detail::jsonQuote;
        auto refJson = [&](const PhandleReference& r) {
            std::ostringstream os;
            os << "{\"source\": " << jsonQuote(r.source->getFullPath()) << ", \"property\": "
               << jsonQuote(r.property) << ", \"target\": "
               << (r.target ? jsonQuote(r.target->getFullPath()) : std::string("null")) << ", \"args\": [";
            for (size_t i = 0; i < r.args.size(); ++i) os << (i ? ", " : "") << r.args[i];
            os << "], \"problem\": " << (r.problem.empty() ? std::string("null") : jsonQuote(r.problem)) << "}";
            return os.str();
        };
        auto list = [&](const std::vector<PhandleReference>& v) {
            std::string out = "[";
            for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + refJson(v[i]);
            return out + "]";
        };
        if (args.size() == 1) {
            std::cout << "{\"references\": " << list(refs) << "}\n";
            return EXIT_SUCCESS;
        }
        auto node = resolveNode(args[1]);
        if (!node) {
            printError("Node not found: " + args[1]);
            return EXIT_FAILURE;
        }
        std::vector<PhandleReference> from, to;
        for (const auto& r : refs) {
            if (r.source == node) from.push_back(r);
            if (r.target == node) to.push_back(r);
        }
        std::cout << "{\"path\": " << jsonQuote(node->getFullPath()) << ", \"refers_to\": " << list(from)
                  << ", \"referenced_by\": " << list(to) << "}\n";
        return EXIT_SUCCESS;
    }
    auto print = [&](const PhandleReference& r, bool withSource) {
        const bool broken = !r.problem.empty();
        std::cout << "  " << (broken ? Colors::RED : "") << formatReference(r, withSource)
                  << (broken ? Colors::RESET : "") << "\n";
    };

    if (args.size() == 1) {
        std::cout << Colors::BOLD << "References (" << refs.size() << "):" << Colors::RESET << "\n";
        for (const auto& r : refs) print(r, true);
        return EXIT_SUCCESS;
    }

    auto node = resolveNode(args[1]);
    if (!node) {
        printError("Node not found: " + args[1]);
        return EXIT_FAILURE;
    }
    std::vector<PhandleReference> from, to;
    for (const auto& r : refs) {
        if (r.source == node) from.push_back(r);
        if (r.target == node) to.push_back(r);
    }
    std::cout << Colors::BOLD << node->getFullPath() << " refers to (" << from.size() << "):" << Colors::RESET << "\n";
    for (const auto& r : from) print(r, false);
    std::cout << Colors::BOLD << node->getFullPath() << " is referenced by (" << to.size() << "):" << Colors::RESET << "\n";
    for (const auto& r : to) {
        std::cout << "  " << r.source->getFullPath() << ":" << r.property << "\n";
    }
    return EXIT_SUCCESS;
}

} // namespace DTE

namespace DTE {

int CLIApp::handleOverlay(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        printError("Usage: " + commands_["overlay"].usage);
        return EXIT_FAILURE;
    }
    const std::string& baseFile = args[0];
    const std::string& outputFile = args[1];
    const std::string ext = getFileExtension(outputFile);
    if (ext != "dtb" && ext != "dtbo" && ext != "dts") {
        printError("Output must end in .dtb, .dtbo or .dts: " + outputFile);
        return EXIT_FAILURE;
    }

    printInfo("Loading base device tree: " + baseFile);
    if (!loadDeviceTree(baseFile)) {
        return EXIT_FAILURE;
    }
    const auto before = std::make_shared<DeviceTree>(*deviceTree_);
    before->setRoot(deviceTree_->getRoot()->clone());
    SymbolMap labels = baseSymbolsFor(*deviceTree_, baseFile, loadOptions());

    for (size_t i = 2; i < args.size(); ++i) {
        printInfo("Applying overlay: " + args[i]);
        DeviceTree overlay;
        std::string error;
        if (!loadOverlay(overlay, args[i], loadOptions(), &error)) {
            printError("Failed to load overlay " + args[i] + "\n" + error);
            return EXIT_FAILURE;
        }
        if (!applyOverlay(*deviceTree_, overlay, &error, &labels)) {
            printError("Failed to apply overlay " + args[i] + "\n" + error);
            return EXIT_FAILURE;
        }
    }

    std::string error;
    if (!deviceTree_->saveToFile(outputFile, ext == "dts", &error)) {
        printError(error);
        return EXIT_FAILURE;
    }

    DeviceTreeDiff diff(before, deviceTree_);
    printSuccess("Applied " + std::to_string(args.size() - 2) + " overlay(s): " +
                 std::to_string(diff.getAddedCount()) + " added, " + std::to_string(diff.getModifiedCount()) +
                 " modified, " + std::to_string(diff.getRemovedCount()) + " removed -> " + outputFile);
    return EXIT_SUCCESS;
}

} // namespace DTE

namespace DTE {

int CLIApp::handleMemmap(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        printError("Usage: " + commands_["memmap"].usage);
        return EXIT_FAILURE;
    }
    if (!fileExists(args[0])) {
        printError("File not found: " + args[0]);
        return EXIT_FAILURE;
    }
    printInfo("Loading device tree from: " + args[0]);
    if (!loadDeviceTree(args[0])) {
        return EXIT_FAILURE;
    }

    struct Region {
        uint64_t start;
        uint64_t size;
        bool hasSize;
        std::string path;
    };
    std::vector<Region> regions;
    std::vector<std::pair<std::string, std::string>> unmapped; // path, description

    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk = [&](const std::shared_ptr<DeviceTreeNode>& n) {
        if (n->getParent() && static_cast<const DeviceTreeNode&>(*n).findProperty("reg")) {
            std::vector<RegEntry> regs;
            std::string error;
            if (!decodeReg(n, regs, &error)) {
                unmapped.emplace_back(n->getFullPath(), error);
            }
            for (const auto& r : regs) {
                if (r.translated) {
                    regions.push_back({r.cpuAddress, r.size, r.hasSize, n->getFullPath()});
                } else {
                    unmapped.emplace_back(n->getFullPath(), describeReg(r));
                }
            }
        }
        for (const auto& c : n->getChildren()) walk(c);
    };
    walk(deviceTree_->getRoot());

    std::stable_sort(regions.begin(), regions.end(),
                     [](const Region& a, const Region& b) { return a.start < b.start; });

    // Overlaps: a region starting inside the furthest-reaching earlier one
    std::vector<std::string> overlapsWith(regions.size());
    size_t overlaps = 0;
    const Region* furthest = nullptr;
    auto lastByte = [](const Region& r) { return r.start + (r.hasSize && r.size ? r.size - 1 : 0); };
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& r = regions[i];
        if (furthest && r.start <= lastByte(*furthest) && r.size && furthest->size && furthest->path != r.path) {
            overlapsWith[i] = furthest->path;
            ++overlaps;
        }
        if (!furthest || lastByte(r) > lastByte(*furthest)) furthest = &r;
    }

    if (json_) {
        using detail::jsonQuote;
        std::cout << "{\"regions\": [";
        for (size_t i = 0; i < regions.size(); ++i) {
            const auto& r = regions[i];
            std::cout << (i ? ", " : "") << "{\"start\": " << r.start << ", \"end\": " << lastByte(r)
                      << ", \"size\": " << (r.hasSize ? std::to_string(r.size) : std::string("null"))
                      << ", \"path\": " << jsonQuote(r.path) << ", \"overlaps\": "
                      << (overlapsWith[i].empty() ? std::string("null") : jsonQuote(overlapsWith[i])) << "}";
        }
        std::cout << "], \"untranslated\": [";
        for (size_t i = 0; i < unmapped.size(); ++i) {
            std::cout << (i ? ", " : "") << "{\"path\": " << jsonQuote(unmapped[i].first) << ", \"reason\": "
                      << jsonQuote(unmapped[i].second) << "}";
        }
        std::cout << "]}\n";
        return EXIT_SUCCESS;
    }

    uint64_t maxAddress = 0;
    for (const auto& r : regions) maxAddress = std::max(maxAddress, lastByte(r));
    const int width = maxAddress > 0xffffffffULL ? 16 : 8;
    auto hex = [width](uint64_t v) {
        std::ostringstream os;
        os << "0x" << std::hex << std::setw(width) << std::setfill('0') << v;
        return os.str();
    };

    std::cout << Colors::BOLD << "Memory map (" << regions.size() << " regions):" << Colors::RESET << "\n";
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& r = regions[i];
        std::ostringstream size;
        if (r.hasSize) size << "0x" << std::hex << r.size; else size << "-";
        std::cout << "  " << hex(r.start) << "-" << hex(lastByte(r)) << "  " << std::setw(width + 2)
                  << std::setfill(' ') << std::left << size.str() << std::right << "  " << Colors::GREEN << r.path
                  << Colors::RESET;
        if (!overlapsWith[i].empty()) {
            std::cout << "  " << Colors::YELLOW << "overlaps " << overlapsWith[i] << Colors::RESET;
        }
        std::cout << "\n";
    }

    if (!unmapped.empty()) {
        std::cout << "\n" << Colors::BOLD << "Not translated (" << unmapped.size() << "):" << Colors::RESET << "\n";
        for (const auto& [path, what] : unmapped) {
            std::cout << "  " << Colors::GREEN << path << Colors::RESET << "  " << what << "\n";
        }
    }
    if (overlaps) {
        printWarning(std::to_string(overlaps) + " overlapping region(s)");
    }
    return EXIT_SUCCESS;
}

} // namespace DTE

namespace DTE {

int CLIApp::handleIrqs(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        printError("Usage: " + commands_["irqs"].usage);
        return EXIT_FAILURE;
    }
    if (!fileExists(args[0])) {
        printError("File not found: " + args[0]);
        return EXIT_FAILURE;
    }
    printInfo("Loading device tree from: " + args[0]);
    if (!loadDeviceTree(args[0])) {
        return EXIT_FAILURE;
    }

    struct Line {
        std::vector<uint32_t> specifier;
        std::string description;
        std::string user; // node path [name]
    };
    std::map<std::string, std::vector<Line>> byController;
    std::vector<std::string> unresolved;

    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> walk = [&](const std::shared_ptr<DeviceTreeNode>& n) {
        const auto& node = static_cast<const DeviceTreeNode&>(*n);
        if (node.findProperty("interrupts") || node.findProperty("interrupts-extended")) {
            std::vector<InterruptSpec> specs;
            std::string error;
            if (!decodeInterrupts(*deviceTree_, n, specs, &error)) {
                unresolved.push_back(n->getFullPath() + "  " + error);
            }
            for (const auto& spec : specs) {
                std::string user = n->getFullPath() + (spec.name.empty() ? "" : " [" + spec.name + "]");
                if (!spec.problem.empty()) {
                    unresolved.push_back(user + "  " + spec.problem);
                    continue;
                }
                std::string description = describeSpecifier(*spec.controller, spec.specifier);
                for (const auto& via : spec.route) description += " via " + via;
                byController[spec.controller->getFullPath()].push_back({spec.specifier, description, user});
            }
        }
        for (const auto& c : n->getChildren()) walk(c);
    };
    walk(deviceTree_->getRoot());

    // Sort each controller's lines; mark lines shared with the previous
    // entry (same cells apart from the trigger/flags cell)
    auto lineOf = [](const std::vector<uint32_t>& spec) {
        return spec.size() >= 2 ? std::vector<uint32_t>(spec.begin(), spec.end() - 1) : spec;
    };
    std::map<std::string, std::vector<std::string>> sharedWith;
    size_t total = 0, shared = 0;
    for (auto& [controller, lines] : byController) {
        std::stable_sort(lines.begin(), lines.end(),
                         [](const Line& a, const Line& b) { return a.specifier < b.specifier; });
        auto& marks = sharedWith[controller];
        marks.assign(lines.size(), "");
        for (size_t i = 1; i < lines.size(); ++i) {
            if (lineOf(lines[i - 1].specifier) == lineOf(lines[i].specifier)) {
                marks[i] = lines[i - 1].user;
                ++shared;
            }
        }
        total += lines.size();
    }

    if (json_) {
        using detail::jsonQuote;
        std::cout << "{\"controllers\": {";
        bool firstController = true;
        for (const auto& [controller, lines] : byController) {
            std::cout << (firstController ? "" : ", ") << jsonQuote(controller) << ": [";
            firstController = false;
            for (size_t i = 0; i < lines.size(); ++i) {
                const auto& l = lines[i];
                std::cout << (i ? ", " : "") << "{\"user\": " << jsonQuote(l.user) << ", \"specifier\": [";
                for (size_t k = 0; k < l.specifier.size(); ++k) std::cout << (k ? ", " : "") << l.specifier[k];
                std::cout << "], \"description\": " << jsonQuote(l.description) << ", \"shared_with\": "
                          << (sharedWith[controller][i].empty() ? std::string("null")
                                                                : jsonQuote(sharedWith[controller][i]))
                          << "}";
            }
            std::cout << "]";
        }
        std::cout << "}, \"unresolved\": [";
        for (size_t i = 0; i < unresolved.size(); ++i) std::cout << (i ? ", " : "") << jsonQuote(unresolved[i]);
        std::cout << "]}\n";
        return EXIT_SUCCESS;
    }

    for (const auto& [controller, lines] : byController) {
        std::cout << Colors::BOLD << controller << " (" << lines.size() << "):" << Colors::RESET << "\n";
        size_t width = 0;
        for (const auto& l : lines) width = std::max(width, l.description.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            std::cout << "  " << std::left << std::setw(static_cast<int>(width)) << l.description << std::right
                      << "  " << Colors::GREEN << l.user << Colors::RESET;
            if (!sharedWith[controller][i].empty()) {
                std::cout << "  " << Colors::YELLOW << "shared with " << sharedWith[controller][i] << Colors::RESET;
            }
            std::cout << "\n";
        }
    }
    if (!unresolved.empty()) {
        std::cout << "\n" << Colors::BOLD << "Unresolved (" << unresolved.size() << "):" << Colors::RESET << "\n";
        for (const auto& u : unresolved) std::cout << "  " << Colors::RED << u << Colors::RESET << "\n";
    }
    printInfo(std::to_string(total) + " interrupt(s) on " + std::to_string(byController.size()) + " controller(s)" +
              (shared ? ", " + std::to_string(shared) + " shared line(s)" : ""));
    return EXIT_SUCCESS;
}

} // namespace DTE

namespace DTE {

std::shared_ptr<DeviceTreeNode> CLIApp::resolveNode(const std::string& pathOrAlias) const {
    if (!deviceTree_) return nullptr;
    if (!pathOrAlias.empty() && pathOrAlias[0] == '/') return deviceTree_->findNodeByPath(pathOrAlias);
    auto aliases = deviceTree_->findNodeByPath("/aliases");
    if (!aliases) return nullptr;
    const auto* alias = static_cast<const DeviceTreeNode&>(*aliases).findProperty(pathOrAlias);
    if (!alias || !alias->isString() || alias->isEmpty()) return nullptr;
    return deviceTree_->findNodeByPath(std::get<std::string>(alias->getValue()));
}

int CLIApp::handleGet(const std::vector<std::string>& args) {
    if (args.size() < 2 || args.size() > 3) {
        printError("Usage: " + commands_["get"].usage);
        return EXIT_FAILURE;
    }
    if (!fileExists(args[0])) {
        printError("File not found: " + args[0]);
        return EXIT_FAILURE;
    }
    if (!loadDeviceTree(args[0])) {
        return EXIT_FAILURE;
    }
    auto node = resolveNode(args[1]);
    if (!node) {
        printError("Node not found: " + args[1]);
        return EXIT_FAILURE;
    }
    const auto& n = static_cast<const DeviceTreeNode&>(*node);
    using detail::jsonQuote;

    if (args.size() == 3) {
        const auto* prop = n.findProperty(args[2]);
        if (!prop) {
            printError("Property not found: " + node->getFullPath() + ":" + args[2]);
            return EXIT_FAILURE;
        }
        if (json_) {
            std::cout << "{\"path\": " << jsonQuote(node->getFullPath()) << ", \"property\": "
                      << jsonQuote(prop->getName()) << ", \"value\": " << propertyValueToJson(prop->getValue())
                      << "}\n";
        } else {
            std::cout << prop->getValueAsString() << "\n";
        }
        return EXIT_SUCCESS;
    }

    if (json_) {
        std::cout << "{\"path\": " << jsonQuote(node->getFullPath()) << ", \"properties\": {";
        const auto& props = n.getProperties();
        for (size_t i = 0; i < props.size(); ++i) {
            std::cout << (i ? ", " : "") << jsonQuote(props[i].getName()) << ": "
                      << propertyValueToJson(props[i].getValue());
        }
        std::cout << "}, \"children\": [";
        const auto& children = n.getChildren();
        for (size_t i = 0; i < children.size(); ++i) {
            std::cout << (i ? ", " : "") << jsonQuote(children[i]->getName());
        }
        std::cout << "]}\n";
        return EXIT_SUCCESS;
    }
    for (const auto& prop : n.getProperties()) {
        std::cout << prop.getName();
        if (!prop.isEmpty()) std::cout << " = " << prop.getValueAsString();
        std::cout << ";\n";
    }
    for (const auto& child : n.getChildren()) {
        std::cout << child->getName() << "/\n";
    }
    return EXIT_SUCCESS;
}

} // namespace DTE
