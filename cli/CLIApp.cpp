#include "CLIApp.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <functional>
#include <unistd.h>

namespace DTE {

// ANSI color codes for better output
namespace Colors {
    const std::string RESET = "\033[0m";
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string BOLD = "\033[1m";
    const std::string UNDERLINE = "\033[4m";
}

// Utility functions
namespace {
    bool isTerminal() {
        return isatty(STDOUT_FILENO);
    }
    
    int getTerminalWidth() {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            return w.ws_col;
        }
        return 80; // Default width
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
    
    bool fileExists(const std::string& filename) {
        std::ifstream file(filename);
        return file.good();
    }
}

CLIApp::CLIApp(int argc, char* argv[])
    : programName_(argv[0]) {
    
    // Convert argv to vector of strings
    for (int i = 1; i < argc; ++i) {
        args_.push_back(argv[i]);
    }
    
    setupCommands();
}

int CLIApp::run() {
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
    
    CLICommand helpCmd;
    helpCmd.name = "help";
    helpCmd.description = "Show help for a command";
    helpCmd.usage = "help [command]";
    helpCmd.handler = [this](const std::vector<std::string>& args) { return handleHelp(args); };
    commands_["help"] = helpCmd;
}

void CLIApp::printUsage() const {
    std::cout << Colors::BOLD << "Device Tree Explorer CLI" << Colors::RESET << "\n\n";
    std::cout << "Usage: " << programName_ << " <command> [options]\n\n";
    std::cout << Colors::BOLD << "Available commands:" << Colors::RESET << "\n";
    
    int maxWidth = 0;
    for (const auto& cmd : commands_) {
        maxWidth = std::max(maxWidth, static_cast<int>(cmd.second.name.length()));
    }
    
    for (const auto& cmd : commands_) {
        std::cout << "  " << std::setw(maxWidth) << std::left << cmd.second.name 
                  << " " << cmd.second.description << "\n";
    }
    
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
        std::cout << "  " << programName_ << " info /proc/device-tree\n";
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
        std::cout << "  dts     - Device Tree Source format\n";
        std::cout << "  dtb     - Device Tree Blob format\n";
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
    
    printValidationResults();
    return deviceTree_->validate() ? EXIT_SUCCESS : EXIT_FAILURE;
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
    if (!baseTree->loadFromFile(baseFile)) {
        printError("Failed to load base file: " + baseFile + "\n" + baseTree->getLoadError());
        return DIFF_EXIT_ERROR;
    }
    
    printInfo("Loading overlay device tree: " + overlayFile);
    auto overlayTree = std::make_shared<DeviceTree>();
    if (!overlayTree->loadFromFile(overlayFile)) {
        printError("Failed to load overlay file: " + overlayFile + "\n" + overlayTree->getLoadError());
        return DIFF_EXIT_ERROR;
    }
    
    printInfo("Generating diff...");
    DeviceTreeDiff diff(baseTree, overlayTree);
    printDiffResults(diff);
    
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
    
    printInfo("Loading device tree from: " + inputFile);
    if (!loadDeviceTree(inputFile)) {
        return EXIT_FAILURE;
    }
    
    printInfo("Exporting to " + format + " format: " + outputFile);
    
    std::string content;
    if (format == "json") {
        content = deviceTree_->exportAsJSON();
    } else if (format == "yaml") {
        content = deviceTree_->exportAsYAML();
    } else {
        printError("Unsupported format: " + format);
        printInfo("Supported formats: json, yaml");
        return EXIT_FAILURE;
    }
    
    std::ofstream outFile(outputFile);
    if (!outFile) {
        printError("Failed to open output file: " + outputFile);
        return EXIT_FAILURE;
    }
    
    outFile << content;
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
    
    // Determine conversion direction based on file extensions
    std::string inputExt = getFileExtension(inputFile);
    std::string outputExt = getFileExtension(outputFile);
    
    printInfo("Converting " + inputExt + " to " + outputExt + ": " + inputFile + " -> " + outputFile);
    
    bool success = false;
    if (inputExt == "dtb" && outputExt == "dts") {
        success = DeviceTreeUtils::dtbToDts(inputFile, outputFile);
    } else if (inputExt == "dts" && outputExt == "dtb") {
        success = DeviceTreeUtils::dtsToDtb(inputFile, outputFile);
    } else {
        printError("Unsupported conversion: " + inputExt + " to " + outputExt);
        printInfo("Supported conversions: dtb <-> dts");
        return EXIT_FAILURE;
    }
    
    if (success) {
        printSuccess("Successfully converted: " + inputFile + " -> " + outputFile);
    } else {
        printError("Conversion failed");
        return EXIT_FAILURE;
    }
    
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
    if (!deviceTree_->loadFromFile(filename)) {
        printError("Failed to load device tree file: " + filename + "\n" + deviceTree_->getLoadError());
        return false;
    }
    return true;
}

void CLIApp::printDeviceTreeInfo() const {
    if (!deviceTree_) return;
    
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

void CLIApp::printValidationResults() const {
    if (!deviceTree_) return;
    
    std::cout << Colors::BOLD << "Validation Results:" << Colors::RESET << "\n";
    
    if (deviceTree_->validate()) {
        printSuccess("Device tree is valid");
    } else {
        printError("Device tree has validation errors:");
        for (const auto& error : deviceTree_->getValidationErrors()) {
            std::cout << "  " << Colors::RED << "• " << error << Colors::RESET << "\n";
        }
    }
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
        std::cout << prefix << "  " << Colors::CYAN << prop.getName() << Colors::RESET 
                  << " = " << Colors::YELLOW << prop.getValueAsString() << Colors::RESET << "\n";
    }
    
    // Print children
    for (const auto& child : node->getChildren()) {
        printTree(child, prefix + "  ");
    }
}

void CLIApp::printError(const std::string& message) const {
    if (isTerminal()) {
        std::cerr << Colors::RED << Colors::BOLD << "ERROR: " << Colors::RESET << message << "\n";
    } else {
        std::cerr << "ERROR: " << message << "\n";
    }
}

void CLIApp::printWarning(const std::string& message) const {
    if (isTerminal()) {
        std::cout << Colors::YELLOW << Colors::BOLD << "WARNING: " << Colors::RESET << message << "\n";
    } else {
        std::cout << "WARNING: " << message << "\n";
    }
}

void CLIApp::printSuccess(const std::string& message) const {
    if (isTerminal()) {
        std::cout << Colors::GREEN << Colors::BOLD << "SUCCESS: " << Colors::RESET << message << "\n";
    } else {
        std::cout << "SUCCESS: " << message << "\n";
    }
}

void CLIApp::printInfo(const std::string& message) const {
    if (isTerminal()) {
        std::cout << Colors::BLUE << Colors::BOLD << "INFO: " << Colors::RESET << message << "\n";
    } else {
        std::cout << "INFO: " << message << "\n";
    }
}

} // namespace DTE 