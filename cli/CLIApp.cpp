#include "CLIApp.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unistd.h>

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

    auto node = deviceTree_->findNodeByPath(args[1]);
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
