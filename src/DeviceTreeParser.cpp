#include "DeviceTreeParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <cassert>
#include <cstdint>
#include <iterator>

namespace DTE {

// DTBParser implementation
namespace {

// Flattened device tree constants (Devicetree Specification, chapter 5)
constexpr uint32_t FDT_MAGIC = 0xd00dfeed;
constexpr uint32_t FDT_BEGIN_NODE = 0x1;
constexpr uint32_t FDT_END_NODE = 0x2;
constexpr uint32_t FDT_PROP = 0x3;
constexpr uint32_t FDT_NOP = 0x4;
constexpr uint32_t FDT_END = 0x9;

constexpr size_t FDT_V16_HEADER_SIZE = 36;
constexpr size_t FDT_V17_HEADER_SIZE = 40;
constexpr uint32_t FDT_FIRST_SUPPORTED_VERSION = 16;
constexpr uint32_t FDT_LAST_SUPPORTED_VERSION = 17;

std::string hex32(uint32_t v) {
    std::ostringstream oss;
    oss << "0x" << std::hex << v;
    return oss.str();
}

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

// Bounds-checked big-endian reader over a byte range.
class BlobReader {
public:
    BlobReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    size_t size() const { return size_; }

    uint32_t be32(size_t off, const char* what) const {
        if (off > size_ || size_ - off < 4) {
            fail(std::string("Truncated blob reading ") + what + " at offset " + std::to_string(off));
        }
        const uint8_t* p = data_ + off;
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    uint64_t be64(size_t off, const char* what) const {
        return (uint64_t(be32(off, what)) << 32) | be32(off + 4, what);
    }

    // Returns the NUL-terminated string starting at off; the terminator must
    // lie inside [off, limit).
    std::string cstr(size_t off, size_t limit, const char* what) const {
        if (limit > size_) limit = size_;
        if (off >= limit) {
            fail(std::string(what) + " starts outside its block (offset " + std::to_string(off) + ")");
        }
        const void* nul = std::memchr(data_ + off, '\0', limit - off);
        if (!nul) {
            fail(std::string(what) + " is not NUL-terminated (offset " + std::to_string(off) + ")");
        }
        return std::string(reinterpret_cast<const char*>(data_ + off),
                           static_cast<const uint8_t*>(nul) - (data_ + off));
    }

    const uint8_t* ptr(size_t off) const { return data_ + off; }

private:
    const uint8_t* data_;
    size_t size_;
};

constexpr size_t align4(size_t v) { return (v + 3) & ~size_t(3); }

// Checks that [off, off + len) lies inside [0, total) without overflow.
bool rangeInside(uint64_t off, uint64_t len, uint64_t total) {
    return off <= total && len <= total - off;
}

bool isPrintable(uint8_t c) { return c >= 0x20 && c <= 0x7e; }

// Infer the most natural representation of a raw property value, using the
// same heuristic as dtc's decompiler:
//   - one or more non-empty printable NUL-terminated strings -> string
//     (a string list is joined as `a", "b` so it renders like DTS source)
//   - length multiple of 4 -> big-endian 32-bit cells
//   - otherwise -> bytes
PropertyValue inferPropertyValue(const uint8_t* data, size_t len) {
    if (len == 0) {
        return std::string();
    }

    if (data[len - 1] == '\0') {
        bool ok = true;
        size_t start = 0;
        std::vector<std::string> parts;
        for (size_t i = 0; i < len && ok; ++i) {
            if (data[i] == '\0') {
                if (i == start) {
                    ok = false; // empty string segment
                } else {
                    parts.emplace_back(reinterpret_cast<const char*>(data + start), i - start);
                    start = i + 1;
                }
            } else if (!isPrintable(data[i])) {
                ok = false;
            }
        }
        if (ok) {
            std::string joined;
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) joined += "\", \"";
                joined += parts[i];
            }
            return joined;
        }
    }

    if (len % 4 == 0) {
        std::vector<uint32_t> cells(len / 4);
        for (size_t i = 0; i < cells.size(); ++i) {
            const uint8_t* p = data + i * 4;
            cells[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                       (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        }
        return cells;
    }

    return std::vector<uint8_t>(data, data + len);
}

std::shared_ptr<DeviceTree> parseFdt(const uint8_t* data, size_t size) {
    BlobReader blob(data, size);

    if (size < FDT_V16_HEADER_SIZE) {
        fail("File too small to be a DTB (" + std::to_string(size) + " bytes)");
    }

    const uint32_t magic = blob.be32(0, "magic");
    if (magic != FDT_MAGIC) {
        fail("Invalid DTB magic " + hex32(magic) + " (expected " + hex32(FDT_MAGIC) + ")");
    }

    const uint32_t totalsize = blob.be32(4, "totalsize");
    const uint32_t offStruct = blob.be32(8, "off_dt_struct");
    const uint32_t offStrings = blob.be32(12, "off_dt_strings");
    const uint32_t offMemRsv = blob.be32(16, "off_mem_rsvmap");
    const uint32_t version = blob.be32(20, "version");
    const uint32_t lastCompVersion = blob.be32(24, "last_comp_version");
    const uint32_t bootCpuId = blob.be32(28, "boot_cpuid_phys");
    const uint32_t sizeStrings = blob.be32(32, "size_dt_strings");

    if (version < FDT_FIRST_SUPPORTED_VERSION) {
        fail("Unsupported DTB version " + std::to_string(version) +
             " (need >= " + std::to_string(FDT_FIRST_SUPPORTED_VERSION) + ")");
    }
    if (lastCompVersion > FDT_LAST_SUPPORTED_VERSION) {
        fail("DTB requires reader version " + std::to_string(lastCompVersion) +
             " (supported up to " + std::to_string(FDT_LAST_SUPPORTED_VERSION) + ")");
    }

    const size_t headerSize = version >= 17 ? FDT_V17_HEADER_SIZE : FDT_V16_HEADER_SIZE;
    if (totalsize < headerSize) {
        fail("Header totalsize " + std::to_string(totalsize) + " is smaller than the header");
    }
    if (totalsize > size) {
        fail("Blob truncated: header says " + std::to_string(totalsize) +
             " bytes, only " + std::to_string(size) + " available");
    }

    // From here on only look at the first totalsize bytes.
    BlobReader fdt(data, totalsize);

    const uint32_t sizeStruct = version >= 17 ? fdt.be32(36, "size_dt_struct")
                                              : totalsize - std::min(offStruct, totalsize);

    if (offStruct % 4 != 0 || !rangeInside(offStruct, sizeStruct, totalsize) || offStruct < headerSize) {
        fail("Structure block out of bounds or misaligned");
    }
    if (!rangeInside(offStrings, sizeStrings, totalsize) || offStrings < headerSize) {
        fail("Strings block out of bounds");
    }
    if (offMemRsv % 8 != 0 || offMemRsv < headerSize || offMemRsv >= totalsize) {
        fail("Memory reservation block out of bounds or misaligned");
    }

    auto tree = std::make_shared<DeviceTree>();
    tree->setBootCpuId(bootCpuId);

    // Memory reservation block: (address, size) pairs terminated by (0, 0).
    std::vector<MemoryReservation> reservations;
    for (size_t off = offMemRsv;; off += 16) {
        if (!rangeInside(off, 16, totalsize)) {
            fail("Memory reservation block is not terminated");
        }
        MemoryReservation r{fdt.be64(off, "memory reservation"),
                            fdt.be64(off + 8, "memory reservation")};
        if (r.address == 0 && r.size == 0) break;
        reservations.push_back(r);
    }
    tree->setMemoryReservations(std::move(reservations));

    // Structure block: walk tokens iteratively with an explicit node stack.
    BlobReader st(fdt.ptr(offStruct), sizeStruct);
    const size_t stringsEnd = size_t(offStrings) + sizeStrings;

    std::vector<std::shared_ptr<DeviceTreeNode>> stack;
    std::shared_ptr<DeviceTreeNode> root;
    size_t pos = 0;

    for (;;) {
        const size_t tokenPos = pos;
        const uint32_t token = st.be32(pos, "structure token");
        pos += 4;

        switch (token) {
        case FDT_BEGIN_NODE: {
            std::string name = st.cstr(pos, st.size(), "Node name");
            pos = align4(pos + name.size() + 1);

            if (stack.empty()) {
                if (root) {
                    fail("Multiple root nodes (second node at structure offset " +
                         std::to_string(tokenPos) + ")");
                }
                // Root is "" in v16+ blobs; very old tools wrote "/".
                if (!name.empty() && name != "/") {
                    fail("Root node has unexpected name '" + name + "'");
                }
                root = std::make_shared<DeviceTreeNode>("/");
                stack.push_back(root);
            } else {
                if (name.empty()) {
                    fail("Empty node name at structure offset " + std::to_string(tokenPos));
                }
                auto node = std::make_shared<DeviceTreeNode>(name);
                stack.back()->addChild(node);
                stack.push_back(node);
            }
            break;
        }

        case FDT_END_NODE:
            if (stack.empty()) {
                fail("Unbalanced FDT_END_NODE at structure offset " + std::to_string(tokenPos));
            }
            stack.pop_back();
            break;

        case FDT_PROP: {
            if (stack.empty()) {
                fail("Property outside of any node at structure offset " + std::to_string(tokenPos));
            }
            const uint32_t len = st.be32(pos, "property length");
            const uint32_t nameoff = st.be32(pos + 4, "property name offset");
            pos += 8;

            if (!rangeInside(pos, len, st.size())) {
                fail("Property value at structure offset " + std::to_string(tokenPos) +
                     " extends past the structure block");
            }
            if (nameoff >= sizeStrings) {
                fail("Property name offset " + std::to_string(nameoff) +
                     " outside strings block (size " + std::to_string(sizeStrings) + ")");
            }
            std::string name = fdt.cstr(size_t(offStrings) + nameoff, stringsEnd, "Property name");
            if (name.empty()) {
                fail("Empty property name at structure offset " + std::to_string(tokenPos));
            }

            stack.back()->addProperty(DeviceTreeProperty(name, inferPropertyValue(st.ptr(pos), len)));
            pos = align4(pos + len);
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            if (!stack.empty()) {
                fail("FDT_END reached with " + std::to_string(stack.size()) + " unclosed node(s)");
            }
            if (!root) {
                fail("Structure block contains no root node");
            }
            tree->setRoot(root);
            return tree;

        default:
            fail("Unknown structure token " + hex32(token) +
                 " at structure offset " + std::to_string(tokenPos));
        }
    }
}

bool hasSuffix(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

std::shared_ptr<DeviceTree> DTBParser::parseBuffer(const uint8_t* data, size_t size) {
    lastError_.clear();
    try {
        return parseFdt(data, size);
    } catch (const std::exception& e) {
        lastError_ = e.what();
    }
    return nullptr;
}

std::shared_ptr<DeviceTree> DTBParser::parse(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        lastError_ = "Cannot open file: " + filename;
        std::cerr << "DTB parsing error: " << lastError_ << std::endl;
        return nullptr;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

    auto tree = parseBuffer(buffer);
    if (!tree) {
        std::cerr << "DTB parsing error: " << lastError_ << std::endl;
        return nullptr;
    }
    tree->setSourceFile(filename);
    return tree;
}

bool DTBParser::canParse(const std::string& filename) const {
    // Prefer content detection: the magic is stored big-endian.
    std::ifstream file(filename, std::ios::binary);
    if (file) {
        uint8_t m[4] = {};
        if (file.read(reinterpret_cast<char*>(m), 4)) {
            uint32_t magic = (uint32_t(m[0]) << 24) | (uint32_t(m[1]) << 16) |
                             (uint32_t(m[2]) << 8) | uint32_t(m[3]);
            if (magic == FDT_MAGIC) {
                return true;
            }
        }
    }

    // Fall back to the extension so a corrupt .dtb gets a DTB error message.
    return hasSuffix(filename, ".dtb") || hasSuffix(filename, ".dtbo");
}

// DTSParser implementation
std::shared_ptr<DeviceTree> DTSParser::parse(const std::string& filename) {
    try {
        auto tree = std::make_shared<DeviceTree>();
        if (parseDTSFile(filename, tree)) {
            tree->setSourceFile(filename);
            return tree;
        }
    } catch (const std::exception& e) {
        std::cerr << "DTS parsing error: " << e.what() << std::endl;
    }
    return nullptr;
}

bool DTSParser::canParse(const std::string& filename) const {
    return filename.find(".dts") != std::string::npos;
}

bool DTSParser::parseDTSFile(const std::string& filename, std::shared_ptr<DeviceTree> tree) {
    std::ifstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    std::string line;
    auto rootNode = parseDTSNode(file, line);
    if (rootNode) {
        tree->setRoot(rootNode);
        return true;
    }
    
    return false;
}

std::shared_ptr<DeviceTreeNode> DTSParser::parseDTSNode(std::istream& stream, std::string& line) {
    // Skip to node definition
    while (std::getline(stream, line)) {
        // Skip comments and empty lines
        std::string trimmedLine = line;
        trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t"));
        if (trimmedLine.empty() || (trimmedLine[0] == '/' && trimmedLine[1] == '/') || trimmedLine[0] == '*') {
            continue;
        }
        
        if (line.find("/dts-v1/;") != std::string::npos) {
            continue; // Skip version header
        }
        
        // Check if this line contains a node definition
        if (line.find("{") != std::string::npos) {
            // Extract node name
            size_t bracePos = line.find("{");
            std::string nodeName = line.substr(0, bracePos);
            
            // Trim whitespace
            nodeName.erase(0, nodeName.find_first_not_of(" \t"));
            nodeName.erase(nodeName.find_last_not_of(" \t") + 1);
            
            // Handle root node
            if (nodeName.empty() || nodeName == "/") {
                nodeName = "/";
            }
            
            auto node = std::make_shared<DeviceTreeNode>(nodeName);
            
            // Parse node content using a stack-based approach
            std::vector<std::shared_ptr<DeviceTreeNode>> nodeStack;
            nodeStack.push_back(node);
            
            while (std::getline(stream, line)) {
                std::string trimmedLine = line;
                trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t"));
                
                if (trimmedLine.find("};") != std::string::npos) {
                    // End of current node, pop from stack
                    if (nodeStack.size() > 1) {
                        nodeStack.pop_back();
                    } else {
                        break; // End of root node
                    }
                    continue;
                }
                
                // Skip empty lines and comments
                if (trimmedLine.empty() || (trimmedLine[0] == '/' && trimmedLine[1] == '/') || trimmedLine[0] == '*') {
                    continue;
                }
                
                // Check if it's a property
                if (line.find("=") != std::string::npos) {
                    try {
                        auto prop = parseDTSProperty(stream, line);
                        nodeStack.back()->addProperty(prop);
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: Failed to parse property: " << e.what() << std::endl;
                        continue;
                    }
                }
                // Check if it's a child node (node name followed by {)
                else if (line.find("{") != std::string::npos) {
                    try {
                        // Extract child node name from current line
                        size_t bracePos = line.find("{");
                        std::string childNodeName = line.substr(0, bracePos);
                        
                        // Trim whitespace
                        childNodeName.erase(0, childNodeName.find_first_not_of(" \t"));
                        childNodeName.erase(childNodeName.find_last_not_of(" \t") + 1);
                        
                        auto childNode = std::make_shared<DeviceTreeNode>(childNodeName);
                        
                        // Add child to current node and push to stack
                        nodeStack.back()->addChild(childNode);
                        nodeStack.push_back(childNode);
                        
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: Failed to parse child node: " << e.what() << std::endl;
                        continue;
                    }
                }
            }
            
            return node;
        }
    }
    
    return nullptr;
}

DeviceTreeProperty DTSParser::parseDTSProperty(std::istream& stream, std::string& line) {
    // Extract property name
    size_t equalPos = line.find("=");
    if (equalPos == std::string::npos) {
        throw std::runtime_error("Invalid property syntax: missing '='");
    }
    
    std::string propName = line.substr(0, equalPos);
    
    // Trim whitespace
    propName.erase(0, propName.find_first_not_of(" \t"));
    propName.erase(propName.find_last_not_of(" \t") + 1);
    
    if (propName.empty()) {
        throw std::runtime_error("Empty property name");
    }
    
    // Parse property value
    std::string value = parseDTSValue(stream, line);
    
    // Determine value type and create property
    PropertyValue propValue;
    if (value.empty()) {
        propValue = std::string("");
    } else if (value[0] == '"' && value[value.length() - 1] == '"') {
        // String value
        propValue = value.substr(1, value.length() - 2);
    } else if (value[0] == '<' && value[value.length() - 1] == '>') {
        // Cell values
        std::vector<uint32_t> cells;
        std::string cellStr = value.substr(1, value.length() - 2);
        std::istringstream cellStream(cellStr);
        std::string cell;
        
        while (std::getline(cellStream, cell, ' ')) {
            if (!cell.empty()) {
                // Remove 0x prefix if present
                if (cell.substr(0, 2) == "0x") {
                    cell = cell.substr(2);
                }
                try {
                    cells.push_back(std::stoul(cell, nullptr, 16));
                } catch (const std::exception& e) {
                    throw std::runtime_error("Invalid cell value: " + cell);
                }
            }
        }
        propValue = cells;
    } else if (value[0] == '[' && value[value.length() - 1] == ']') {
        // Binary values
        std::vector<uint8_t> binary;
        std::string binaryStr = value.substr(1, value.length() - 2);
        std::istringstream binaryStream(binaryStr);
        std::string byte;
        
        while (std::getline(binaryStream, byte, ' ')) {
            if (!byte.empty()) {
                // Remove 0x prefix if present
                if (byte.substr(0, 2) == "0x") {
                    byte = byte.substr(2);
                }
                try {
                    binary.push_back(std::stoul(byte, nullptr, 16));
                } catch (const std::exception& e) {
                    throw std::runtime_error("Invalid binary value: " + byte);
                }
            }
        }
        propValue = binary;
    } else {
        // Treat as string
        propValue = value;
    }
    
    return DeviceTreeProperty(propName, propValue);
}

std::string DTSParser::parseDTSValue(std::istream& stream, std::string& line) {
    size_t equalPos = line.find("=");
    std::string value = line.substr(equalPos + 1);
    
    // Trim whitespace
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    
    // Remove semicolon if present
    if (!value.empty() && value[value.length() - 1] == ';') {
        value = value.substr(0, value.length() - 1);
    }
    
    // Handle multi-line values
    if (value.find("\\") != std::string::npos) {
        std::string fullValue = value;
        while (std::getline(stream, line)) {
            if (line.find(";") != std::string::npos) {
                fullValue += line.substr(0, line.find(";"));
                break;
            }
            fullValue += line;
        }
        return fullValue;
    }
    
    return value;
}

// DeviceTreeParserFactory implementation
std::unique_ptr<DeviceTreeParser> DeviceTreeParserFactory::createParser(const std::string& filename) {
    auto parsers = getAllParsers();
    
    for (auto& parser : parsers) {
        if (parser->canParse(filename)) {
            return std::move(parser);
        }
    }
    
    return nullptr;
}

std::vector<std::unique_ptr<DeviceTreeParser>> DeviceTreeParserFactory::getAllParsers() {
    std::vector<std::unique_ptr<DeviceTreeParser>> parsers;
    parsers.push_back(std::make_unique<DTBParser>());
    parsers.push_back(std::make_unique<DTSParser>());
    return parsers;
}

// DeviceTreeUtils implementation
namespace DeviceTreeUtils {

bool dtbToDts(const std::string& dtbFile, const std::string& dtsFile) {
#ifdef HAVE_DTC
    // Use system dtc command
    std::string command = "dtc -I dtb -O dts -o " + dtsFile + " " + dtbFile + " 2>/dev/null";
    return system(command.c_str()) == 0;
#else
    std::cerr << "Warning: dtc not available - conversion not supported" << std::endl;
    return false;
#endif
}

bool dtsToDtb(const std::string& dtsFile, const std::string& dtbFile) {
#ifdef HAVE_DTC
    // Use system dtc command
    std::string command = "dtc -I dts -O dtb -o " + dtbFile + " " + dtsFile + " 2>/dev/null";
    return system(command.c_str()) == 0;
#else
    std::cerr << "Warning: dtc not available - conversion not supported" << std::endl;
    return false;
#endif
}

bool validateDeviceTree(const std::string& filename) {
#ifdef HAVE_DTC
    // Use system dtc command for validation
    std::string command = "dtc -I dts -O dts " + filename + " >/dev/null 2>&1";
    return system(command.c_str()) == 0;
#else
    std::cerr << "Warning: dtc not available - using basic validation only" << std::endl;
    // Basic validation without dtc
    std::ifstream file(filename);
    return file.good();
#endif
}

std::string getDeviceTreeInfo(const std::string& filename) {
#ifdef HAVE_DTC
    // Use system dtc command to get info
    std::string command = "dtc -I dts -O dts " + filename + " 2>&1";
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return "";
    
    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    pclose(pipe);
    return result;
#else
    return "dtc not available - limited information";
#endif
}

bool extractFromKernel(const std::string& kernelFile, const std::string& outputFile) {
    // Use system command to extract device tree from kernel
    std::string command = "scripts/extract-dtb.py -o " + outputFile + " " + kernelFile + " 2>/dev/null";
    return system(command.c_str()) == 0;
}

} // namespace DeviceTreeUtils

} // namespace DTE 