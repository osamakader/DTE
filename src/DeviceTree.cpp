#include "DeviceTree.h"
#include "DeviceTreeParser.h"
#include "Escape.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <map>
#include <cctype>
#include <functional>
#include <unistd.h>
#include <filesystem>


namespace DTE {

// DeviceTreeProperty implementation
DeviceTreeProperty::DeviceTreeProperty(const std::string& name, const PropertyValue& value)
    : name_(name), value_(value) {}

std::string quoteDtsString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c >= 0x7f) {
                static const char* hex = "0123456789abcdef";
                out += "\\x";
                out += hex[c >> 4];
                out += hex[c & 0xf];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += "\"";
    return out;
}

bool DeviceTreeProperty::isString() const {
    return std::holds_alternative<std::string>(value_);
}

bool DeviceTreeProperty::isEmpty() const {
    const auto* str = std::get_if<std::string>(&value_);
    return str && str->empty();
}

bool DeviceTreeProperty::isStringList() const {
    return std::holds_alternative<StringList>(value_);
}

bool DeviceTreeProperty::isBinary() const {
    return std::holds_alternative<std::vector<uint8_t>>(value_);
}

bool DeviceTreeProperty::isCells() const {
    return std::holds_alternative<std::vector<uint32_t>>(value_);
}

bool DeviceTreeProperty::isCells64() const {
    return std::holds_alternative<std::vector<uint64_t>>(value_);
}

std::string DeviceTreeProperty::getValueAsString() const {
    std::ostringstream oss;
    
    if (isEmpty()) {
        return {};
    } else if (isString()) {
        oss << quoteDtsString(std::get<std::string>(value_));
    } else if (isStringList()) {
        const auto& list = std::get<StringList>(value_);
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << quoteDtsString(list[i]);
        }
    } else if (isBinary()) {
        const auto& binary = std::get<std::vector<uint8_t>>(value_);
        oss << "[" << std::hex << std::setfill('0');
        for (size_t i = 0; i < binary.size(); ++i) {
            if (i > 0) oss << " ";
            oss << std::setw(2) << static_cast<int>(binary[i]);
        }
        oss << "]";
    } else if (isCells()) {
        const auto& cells = std::get<std::vector<uint32_t>>(value_);
        oss << "<" << std::hex;
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i > 0) oss << " ";
            oss << "0x" << cells[i];
        }
        oss << ">";
    } else if (isCells64()) {
        const auto& cells = std::get<std::vector<uint64_t>>(value_);
        oss << "/bits/ 64 <" << std::hex;
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i > 0) oss << " ";
            oss << "0x" << cells[i];
        }
        oss << ">";
    }
    
    return oss.str();
}

StringList DeviceTreeProperty::getValueAsStrings() const {
    if (isString()) return {std::get<std::string>(value_)};
    if (isStringList()) return std::get<StringList>(value_);
    return {};
}

std::vector<uint8_t> DeviceTreeProperty::getValueAsBinary() const {
    if (isBinary()) {
        return std::get<std::vector<uint8_t>>(value_);
    }
    return {};
}

std::vector<uint32_t> DeviceTreeProperty::getValueAsCells() const {
    if (isCells()) {
        return std::get<std::vector<uint32_t>>(value_);
    }
    return {};
}

std::vector<uint64_t> DeviceTreeProperty::getValueAsCells64() const {
    if (isCells64()) {
        return std::get<std::vector<uint64_t>>(value_);
    }
    return {};
}

// DeviceTreeNode implementation
DeviceTreeNode::DeviceTreeNode(const std::string& name)
    : name_(name) {}

void DeviceTreeNode::addChild(std::shared_ptr<DeviceTreeNode> child) {
    insertChild(children_.size(), std::move(child));
}

void DeviceTreeNode::insertChild(size_t index, std::shared_ptr<DeviceTreeNode> child) {
    if (!child) return;
    child->parent_ = shared_from_this();
    index = std::min(index, children_.size());
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
}

void DeviceTreeNode::removeChild(std::shared_ptr<DeviceTreeNode> child) {
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        (*it)->parent_.reset();
        children_.erase(it);
    }
}

int DeviceTreeNode::childIndex(const DeviceTreeNode* child) const {
    for (size_t i = 0; i < children_.size(); ++i) {
        if (children_[i].get() == child) return static_cast<int>(i);
    }
    return -1;
}

std::shared_ptr<DeviceTreeNode> DeviceTreeNode::clone() const {
    auto copy = std::make_shared<DeviceTreeNode>(name_);
    copy->properties_ = properties_;
    for (const auto& child : children_) {
        copy->addChild(child->clone());
    }
    return copy;
}

std::shared_ptr<DeviceTreeNode> DeviceTreeNode::findChild(const std::string& name) const {
    for (const auto& c : children_) {
        if (c->getName() == name) return c;
    }
    return nullptr;
}

void DeviceTreeNode::addProperty(const DeviceTreeProperty& property) {
    if (auto* existing = findProperty(property.getName())) {
        *existing = property;
    } else {
        properties_.push_back(property);
    }
}

void DeviceTreeNode::insertProperty(size_t index, const DeviceTreeProperty& property) {
    removeProperty(property.getName());
    index = std::min(index, properties_.size());
    properties_.insert(properties_.begin() + static_cast<std::ptrdiff_t>(index), property);
}

int DeviceTreeNode::propertyIndex(const std::string& name) const {
    for (size_t i = 0; i < properties_.size(); ++i) {
        if (properties_[i].getName() == name) return static_cast<int>(i);
    }
    return -1;
}

void DeviceTreeNode::removeProperty(const std::string& name) {
    properties_.erase(
        std::remove_if(properties_.begin(), properties_.end(),
            [&name](const DeviceTreeProperty& prop) {
                return prop.getName() == name;
            }),
        properties_.end()
    );
}

DeviceTreeProperty* DeviceTreeNode::findProperty(const std::string& name) {
    for (auto& prop : properties_) {
        if (prop.getName() == name) {
            return &prop;
        }
    }
    return nullptr;
}

const DeviceTreeProperty* DeviceTreeNode::findProperty(const std::string& name) const {
    for (const auto& prop : properties_) {
        if (prop.getName() == name) {
            return &prop;
        }
    }
    return nullptr;
}

std::string DeviceTreeNode::getFullPath() const {
    std::vector<std::string> pathComponents;
    std::shared_ptr<const DeviceTreeNode> current = shared_from_this();
    
    while (current) {
        pathComponents.insert(pathComponents.begin(), current->getName());
        current = current->getParent();
    }
    
    std::string path;
    for (const auto& component : pathComponents) {
        if (component != "/") {
            path += "/" + component;
        }
    }
    
    return path.empty() ? "/" : path;
}

std::shared_ptr<DeviceTreeNode> DeviceTreeNode::findNodeByPath(const std::string& path) const {
    if (path.empty() || path == "/") {
        return const_cast<DeviceTreeNode*>(this)->shared_from_this();
    }
    
    std::string currentPath = path;
    if (currentPath[0] == '/') {
        currentPath = currentPath.substr(1);
    }
    
    std::shared_ptr<DeviceTreeNode> current = const_cast<DeviceTreeNode*>(this)->shared_from_this();
    
    std::istringstream pathStream(currentPath);
    std::string component;
    
    while (std::getline(pathStream, component, '/')) {
        if (component.empty()) continue;
        
        bool found = false;
        for (const auto& child : current->getChildren()) {
            if (child->getName() == component) {
                current = child;
                found = true;
                break;
            }
        }
        
        if (!found) {
            return nullptr;
        }
    }
    
    return current;
}

std::vector<std::shared_ptr<DeviceTreeNode>> DeviceTreeNode::findNodesByName(const std::string& name) const {
    std::vector<std::shared_ptr<DeviceTreeNode>> result;
    
    if (name_ == name) {
        result.push_back(const_cast<DeviceTreeNode*>(this)->shared_from_this());
    }
    
    for (const auto& child : children_) {
        auto childResults = child->findNodesByName(name);
        result.insert(result.end(), childResults.begin(), childResults.end());
    }
    
    return result;
}

std::vector<std::shared_ptr<DeviceTreeNode>> DeviceTreeNode::findNodesByPattern(const std::string& pattern) const {
    std::vector<std::shared_ptr<DeviceTreeNode>> result;
    
    // Check if this node's name contains the pattern (case-insensitive)
    if (name_.find(pattern) != std::string::npos) {
        result.push_back(const_cast<DeviceTreeNode*>(this)->shared_from_this());
    }
    
    // Search in children
    for (const auto& child : children_) {
        auto childResults = child->findNodesByPattern(pattern);
        result.insert(result.end(), childResults.begin(), childResults.end());
    }
    
    return result;
}

// DeviceTree implementation
DeviceTree::DeviceTree()
    : root_(std::make_shared<DeviceTreeNode>("/")) {}

bool DeviceTree::loadFromFile(const std::string& filename, const LoadOptions& options) {
    auto parser = DeviceTreeParserFactory::createParser(filename);
    if (!parser) {
        std::error_code ec;
        if (!std::filesystem::exists(filename, ec)) {
            loadError_ = "Cannot open file: " + filename;
        } else if (::access(filename.c_str(), R_OK) != 0) {
            loadError_ = "Permission denied: " + filename +
                         (filename == "/sys/firmware/fdt" ? " (needs root; /proc/device-tree is readable by all users)" : "");
        } else {
            loadError_ = "Unrecognized file format (expected a DTB, a .dts/.dtsi/.dtso file or a "
                         "/proc/device-tree style directory): " + filename;
        }
        return false;
    }
    
    if (auto* dts = dynamic_cast<DTSParser*>(parser.get())) {
        dts->setIncludePaths(options.includePaths);
        dts->setSymbols(options.symbols);
    }
    
    auto parsedTree = parser->parse(filename);
    if (!parsedTree) {
        loadError_ = parser->lastError();
        return false;
    }
    
    // Take over the parsed tree, including header metadata
    *this = *parsedTree;
    sourceFile_ = filename;
    loadError_.clear();
    return true;
}

bool DeviceTree::saveToFile(const std::string& filename, bool asDTS, std::string* error) const {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (!root_) {
        return fail("No device tree to save");
    }

    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out) {
        return fail("Cannot open file for writing: " + filename);
    }
    if (asDTS) {
        out << exportAsDTS();
    } else {
        auto blob = exportAsDTB();
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    }
    out.close();
    if (!out) {
        return fail("Failed to write file: " + filename);
    }
    return true;
}

// DTS writer
namespace {

void writeDtsNode(std::ostream& os, const DeviceTreeNode& node, int depth) {
    const std::string in(depth, '\t');
    os << in << node.getName() << " {\n";

    for (const auto& prop : node.getProperties()) {
        os << in << '\t' << prop.getName();
        if (!prop.isEmpty()) {
            os << " = " << prop.getValueAsString();
        }
        os << ";\n";
    }

    const auto& children = node.getChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        if (i > 0 || !node.getProperties().empty()) os << "\n";
        writeDtsNode(os, *children[i], depth + 1);
    }

    os << in << "};\n";
}

} // namespace

std::string DeviceTree::exportAsDTS() const {
    std::ostringstream os;
    os << "/dts-v1/;\n\n";
    if (!memoryReservations_.empty()) {
        os << std::hex;
        for (const auto& r : memoryReservations_) {
            os << "/memreserve/ 0x" << r.address << " 0x" << r.size << ";\n";
        }
        os << std::dec << "\n";
    }
    if (root_) {
        writeDtsNode(os, *root_, 0);
    }
    return os.str();
}

// DTB writer (Devicetree Specification, chapter 5)
namespace {

class FdtWriter {
public:
    void beginNode(const std::string& name) {
        be32(structBlock_, 0x1); // FDT_BEGIN_NODE
        structBlock_.insert(structBlock_.end(), name.begin(), name.end());
        structBlock_.push_back(0);
        pad(structBlock_);
    }

    void endNode() { be32(structBlock_, 0x2); } // FDT_END_NODE

    void property(const std::string& name, const std::vector<uint8_t>& value) {
        be32(structBlock_, 0x3); // FDT_PROP
        be32(structBlock_, static_cast<uint32_t>(value.size()));
        be32(structBlock_, stringOffset(name));
        structBlock_.insert(structBlock_.end(), value.begin(), value.end());
        pad(structBlock_);
    }

    std::vector<uint8_t> finish(const std::vector<MemoryReservation>& reservations, uint32_t bootCpu) {
        be32(structBlock_, 0x9); // FDT_END

        constexpr size_t headerSize = 40;
        const size_t rsvOff = headerSize;  // already 8-byte aligned
        const size_t structOff = rsvOff + (reservations.size() + 1) * 16;
        const size_t stringsOff = structOff + structBlock_.size();
        const size_t total = stringsOff + stringsBlock_.size();

        std::vector<uint8_t> out;
        out.reserve(total);
        be32(out, 0xd00dfeed);
        be32(out, static_cast<uint32_t>(total));
        be32(out, static_cast<uint32_t>(structOff));
        be32(out, static_cast<uint32_t>(stringsOff));
        be32(out, static_cast<uint32_t>(rsvOff));
        be32(out, 17); // version
        be32(out, 16); // last compatible version
        be32(out, bootCpu);
        be32(out, static_cast<uint32_t>(stringsBlock_.size()));
        be32(out, static_cast<uint32_t>(structBlock_.size()));

        for (const auto& r : reservations) {
            be64(out, r.address);
            be64(out, r.size);
        }
        be64(out, 0);
        be64(out, 0);

        out.insert(out.end(), structBlock_.begin(), structBlock_.end());
        out.insert(out.end(), stringsBlock_.begin(), stringsBlock_.end());
        return out;
    }

    static void be32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(uint8_t(v >> 24));
        buf.push_back(uint8_t(v >> 16));
        buf.push_back(uint8_t(v >> 8));
        buf.push_back(uint8_t(v));
    }

    static void be64(std::vector<uint8_t>& buf, uint64_t v) {
        be32(buf, uint32_t(v >> 32));
        be32(buf, uint32_t(v));
    }

private:
    std::vector<uint8_t> structBlock_;
    std::vector<uint8_t> stringsBlock_;
    std::map<std::string, uint32_t> stringOffsets_;

    static void pad(std::vector<uint8_t>& buf) {
        while (buf.size() % 4) buf.push_back(0);
    }

    uint32_t stringOffset(const std::string& name) {
        auto [it, inserted] = stringOffsets_.try_emplace(name, static_cast<uint32_t>(stringsBlock_.size()));
        if (inserted) {
            stringsBlock_.insert(stringsBlock_.end(), name.begin(), name.end());
            stringsBlock_.push_back(0);
        }
        return it->second;
    }
};

std::vector<uint8_t> encodeValue(const PropertyValue& value) {
    std::vector<uint8_t> out;
    if (auto* str = std::get_if<std::string>(&value)) {
        // The empty string represents an empty property (zero length).
        if (!str->empty()) {
            out.assign(str->begin(), str->end());
            out.push_back(0);
        }
    } else if (auto* list = std::get_if<StringList>(&value)) {
        for (const auto& item : *list) {
            out.insert(out.end(), item.begin(), item.end());
            out.push_back(0);
        }
    } else if (auto* bytes = std::get_if<std::vector<uint8_t>>(&value)) {
        out = *bytes;
    } else if (auto* cells = std::get_if<std::vector<uint32_t>>(&value)) {
        for (auto c : *cells) FdtWriter::be32(out, c);
    } else if (auto* cells64 = std::get_if<std::vector<uint64_t>>(&value)) {
        for (auto c : *cells64) FdtWriter::be64(out, c);
    }
    return out;
}

void writeFdtNode(FdtWriter& w, const DeviceTreeNode& node, bool isRoot) {
    w.beginNode(isRoot ? "" : node.getName());
    for (const auto& prop : node.getProperties()) {
        w.property(prop.getName(), encodeValue(prop.getValue()));
    }
    for (const auto& child : node.getChildren()) {
        writeFdtNode(w, *child, false);
    }
    w.endNode();
}

} // namespace

std::vector<uint8_t> propertyValueToBytes(const PropertyValue& value) {
    return encodeValue(value);
}

std::vector<uint8_t> DeviceTree::exportAsDTB() const {
    FdtWriter w;
    if (root_) {
        writeFdtNode(w, *root_, true);
    } else {
        w.beginNode("");
        w.endNode();
    }
    return w.finish(memoryReservations_, bootCpuId_);
}


// JSON / YAML export
//
// Both formats share one schema:
//   device-tree:
//     source-file, boot-cpuid, memory-reservations: [{address, size}],
//     root-node: {name, path, properties: {name: value}, children: [...]}
// Property values map to: string -> string, string list -> array of
// strings, cells / bytes -> array of numbers, empty property -> "".
// Node and property order is preserved.
namespace {

using detail::jsonQuote;

template <typename T>
void writeNumberList(std::ostream& os, const std::vector<T>& v) {
    os << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) os << ", ";
        os << static_cast<uint64_t>(v[i]);
    }
    os << "]";
}

// Writes a property value as an inline JSON value. The same text is a
// valid YAML flow value, so both writers use it.
void writeInlineValue(std::ostream& os, const DeviceTreeProperty& prop) {
    const auto& v = prop.getValue();
    if (auto* str = std::get_if<std::string>(&v)) {
        os << jsonQuote(*str);
    } else if (auto* list = std::get_if<StringList>(&v)) {
        os << "[";
        for (size_t i = 0; i < list->size(); ++i) {
            if (i > 0) os << ", ";
            os << jsonQuote((*list)[i]);
        }
        os << "]";
    } else if (auto* bytes = std::get_if<std::vector<uint8_t>>(&v)) {
        writeNumberList(os, *bytes);
    } else if (auto* cells = std::get_if<std::vector<uint32_t>>(&v)) {
        writeNumberList(os, *cells);
    } else if (auto* cells64 = std::get_if<std::vector<uint64_t>>(&v)) {
        writeNumberList(os, *cells64);
    }
}

void writeJsonNode(std::ostream& os, const DeviceTreeNode& node, int depth) {
    const std::string in(depth * 2, ' ');
    os << "{\n";
    os << in << "  \"name\": " << jsonQuote(node.getName()) << ",\n";
    os << in << "  \"path\": " << jsonQuote(node.getFullPath()) << ",\n";

    const auto& props = node.getProperties();
    os << in << "  \"properties\": {";
    for (size_t i = 0; i < props.size(); ++i) {
        os << (i == 0 ? "\n" : ",\n") << in << "    " << jsonQuote(props[i].getName()) << ": ";
        writeInlineValue(os, props[i]);
    }
    os << (props.empty() ? "},\n" : "\n" + in + "  },\n");

    const auto& children = node.getChildren();
    os << in << "  \"children\": [";
    for (size_t i = 0; i < children.size(); ++i) {
        os << (i == 0 ? "\n" : ",\n") << in << "    ";
        writeJsonNode(os, *children[i], depth + 2);
    }
    os << (children.empty() ? "]\n" : "\n" + in + "  ]\n");
    os << in << "}";
}

// `lead` is written before the first key: indentation, or "- " for a
// sequence item; following keys are indented to line up with it.
void writeYamlNode(std::ostream& os, const DeviceTreeNode& node, const std::string& lead,
                   const std::string& in) {
    os << lead << "name: " << jsonQuote(node.getName()) << "\n";
    os << in << "path: " << jsonQuote(node.getFullPath()) << "\n";

    const auto& props = node.getProperties();
    os << in << "properties:" << (props.empty() ? " {}\n" : "\n");
    for (const auto& prop : props) {
        // Keys are quoted: names like #address-cells would start a comment.
        os << in << "  " << jsonQuote(prop.getName()) << ": ";
        writeInlineValue(os, prop);
        os << "\n";
    }

    const auto& children = node.getChildren();
    os << in << "children:" << (children.empty() ? " []\n" : "\n");
    for (const auto& child : children) {
        writeYamlNode(os, *child, in + "  - ", in + "    ");
    }
}

} // namespace

std::string DeviceTree::exportAsJSON() const {
    std::ostringstream os;
    os << "{\n";
    os << "  \"device-tree\": {\n";
    os << "    \"source-file\": " << jsonQuote(sourceFile_) << ",\n";
    os << "    \"boot-cpuid\": " << bootCpuId_ << ",\n";
    os << "    \"memory-reservations\": [";
    for (size_t i = 0; i < memoryReservations_.size(); ++i) {
        const auto& r = memoryReservations_[i];
        os << (i == 0 ? "\n" : ",\n") << "      {\"address\": " << r.address
           << ", \"size\": " << r.size << "}";
    }
    os << (memoryReservations_.empty() ? "],\n" : "\n    ],\n");
    os << "    \"root-node\": ";
    if (root_) {
        writeJsonNode(os, *root_, 2);
    } else {
        os << "null";
    }
    os << "\n  }\n";
    os << "}\n";
    return os.str();
}

std::string DeviceTree::exportAsYAML() const {
    std::ostringstream os;
    os << "device-tree:\n";
    os << "  source-file: " << jsonQuote(sourceFile_) << "\n";
    os << "  boot-cpuid: " << bootCpuId_ << "\n";
    os << "  memory-reservations:" << (memoryReservations_.empty() ? " []\n" : "\n");
    for (const auto& r : memoryReservations_) {
        os << "    - {address: " << r.address << ", size: " << r.size << "}\n";
    }
    if (root_) {
        os << "  root-node:\n";
        writeYamlNode(os, *root_, "    ", "    ");
    } else {
        os << "  root-node: null\n";
    }
    return os.str();
}

std::shared_ptr<DeviceTreeNode> DeviceTree::findNodeByPath(const std::string& path) const {
    if (!root_) return nullptr;
    return root_->findNodeByPath(path);
}

std::vector<std::shared_ptr<DeviceTreeNode>> DeviceTree::findNodesByName(const std::string& name) const {
    if (!root_) return {};
    return root_->findNodesByName(name);
}

std::vector<std::shared_ptr<DeviceTreeNode>> DeviceTree::findNodesByPattern(const std::string& pattern) const {
    if (!root_) return {};
    return root_->findNodesByPattern(pattern);
}

namespace {

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::vector<SearchMatch> DeviceTree::search(const std::string& query, const SearchOptions& options) const {
    std::vector<SearchMatch> matches;
    if (!root_ || query.empty()) return matches;

    const std::string needle = options.caseSensitive ? query : lowerAscii(query);
    const bool matchPath = query.find('/') != std::string::npos;
    auto contains = [&](const std::string& hay) {
        return (options.caseSensitive ? hay : lowerAscii(hay)).find(needle) != std::string::npos;
    };

    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> visit =
        [&](const std::shared_ptr<DeviceTreeNode>& node) {
            if (contains(matchPath ? node->getFullPath() : node->getName())) {
                matches.push_back({node, {}});
            }
            if (options.properties) {
                for (const auto& prop : node->getProperties()) {
                    if (contains(prop.getName()) || contains(prop.getValueAsString())) {
                        matches.push_back({node, prop.getName()});
                    }
                }
            }
            for (const auto& child : node->getChildren()) visit(child);
        };
    visit(root_);
    return matches;
}

} // namespace DTE 