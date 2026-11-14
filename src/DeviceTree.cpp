#include "DeviceTree.h"
#include "DeviceTreeParser.h"
#include "Escape.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>


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
    
    if (isString()) {
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
    if (child) {
        child->parent_ = shared_from_this();
        children_.push_back(child);
    }
}

void DeviceTreeNode::removeChild(std::shared_ptr<DeviceTreeNode> child) {
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        children_.erase(it);
    }
}

void DeviceTreeNode::addProperty(const DeviceTreeProperty& property) {
    // Remove existing property with same name
    removeProperty(property.getName());
    properties_.push_back(property);
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
        loadError_ = std::ifstream(filename)
            ? "Unrecognized file format (expected a DTB or a .dts/.dtsi/.dtso file): " + filename
            : "Cannot open file: " + filename;
        return false;
    }
    
    if (auto* dts = dynamic_cast<DTSParser*>(parser.get())) {
        dts->setIncludePaths(options.includePaths);
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

bool DeviceTree::saveToFile(const std::string& filename, bool asDTS) const {
    // TODO: Implement file saving
    (void)filename;
    (void)asDTS;
    return true;
}

bool DeviceTree::validate() const {
    validationErrors_.clear();
    
    if (!root_) {
        validationErrors_.push_back("No root node");
        return false;
    }
    
    // Basic validation - check for required properties
    if (!root_->findProperty("compatible")) {
        validationErrors_.push_back("Root node missing 'compatible' property");
    }
    
    return validationErrors_.empty();
}

std::vector<std::string> DeviceTree::getValidationErrors() const {
    return validationErrors_;
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

} // namespace DTE 