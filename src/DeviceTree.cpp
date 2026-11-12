#include "DeviceTree.h"
#include "DeviceTreeParser.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>

#ifdef HAVE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif

#ifdef HAVE_YAML_CPP
#include <yaml-cpp/yaml.h>
#endif

namespace DTE {

// DeviceTreeProperty implementation
DeviceTreeProperty::DeviceTreeProperty(const std::string& name, const PropertyValue& value)
    : name_(name), value_(value) {}

bool DeviceTreeProperty::isString() const {
    return std::holds_alternative<std::string>(value_);
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
        oss << "\"" << std::get<std::string>(value_) << "\"";
    } else if (isBinary()) {
        auto binary = std::get<std::vector<uint8_t>>(value_);
        oss << "[";
        for (size_t i = 0; i < binary.size(); ++i) {
            if (i > 0) oss << " ";
            oss << "0x" << std::hex << std::setw(2) << std::setfill('0') 
                << static_cast<int>(binary[i]);
        }
        oss << "]";
    } else if (isCells()) {
        auto cells = std::get<std::vector<uint32_t>>(value_);
        oss << "<";
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i > 0) oss << " ";
            oss << "0x" << std::hex << cells[i];
        }
        oss << ">";
    } else if (isCells64()) {
        auto cells = std::get<std::vector<uint64_t>>(value_);
        oss << "<";
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i > 0) oss << " ";
            oss << "0x" << std::hex << cells[i];
        }
        oss << ">";
    }
    
    return oss.str();
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
        } else {
            path = "/";
        }
    }
    
    return path;
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

bool DeviceTree::loadFromFile(const std::string& filename) {
    // Use parser factory to load the file
    auto parser = DeviceTreeParserFactory::createParser(filename);
    if (!parser) {
        return false;
    }
    
    auto parsedTree = parser->parse(filename);
    if (!parsedTree) {
        return false;
    }
    
    // Take over the parsed tree, including header metadata
    *this = *parsedTree;
    sourceFile_ = filename;
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

std::string DeviceTree::exportAsJSON() const {
    if (!root_) return "{}";
    
#ifdef HAVE_NLOHMANN_JSON
    // Use nlohmann/json library
    nlohmann::json json;
    json["device-tree"]["source-file"] = sourceFile_;
    json["device-tree"]["root-node"] = exportNodeToJSON(root_);
    return json.dump(2); // Pretty print with 2-space indent
#else
    // Fallback to custom implementation
    std::ostringstream json;
    json << "{\n";
    json << "  \"device-tree\": {\n";
    json << "    \"source-file\": \"" << sourceFile_ << "\",\n";
    json << "    \"root-node\": ";
    exportNodeAsJSON(root_, json, 2);
    json << "\n  }\n";
    json << "}";
    
    return json.str();
#endif
}

std::string DeviceTree::exportAsYAML() const {
    if (!root_) return "";
    
#ifdef HAVE_YAML_CPP
    // Use yaml-cpp library
    YAML::Node yaml;
    yaml["device-tree"]["source-file"] = sourceFile_;
    yaml["device-tree"]["root-node"] = exportNodeToYAML(root_);
    
    std::ostringstream output;
    output << yaml;
    return output.str();
#else
    // Fallback to custom implementation
    std::ostringstream yaml;
    yaml << "device-tree:\n";
    yaml << "  source-file: " << sourceFile_ << "\n";
    yaml << "  root-node:\n";
    exportNodeAsYAML(root_, yaml, 2);
    
    return yaml.str();
#endif
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

#ifdef HAVE_NLOHMANN_JSON
nlohmann::json DeviceTree::exportNodeToJSON(std::shared_ptr<DeviceTreeNode> node) const {
    nlohmann::json json;
    json["name"] = node->getName();
    
    // Export properties
    nlohmann::json props;
    for (const auto& prop : node->getProperties()) {
        if (prop.isCells()) {
            auto cells = prop.getValueAsCells();
            nlohmann::json cellArray = nlohmann::json::array();
            for (auto cell : cells) {
                cellArray.push_back(cell);
            }
            props[prop.getName()] = cellArray;
        } else if (prop.isBinary()) {
            auto binary = prop.getValueAsBinary();
            nlohmann::json binaryArray = nlohmann::json::array();
            for (auto byte : binary) {
                binaryArray.push_back(static_cast<int>(byte));
            }
            props[prop.getName()] = binaryArray;
        } else {
            props[prop.getName()] = prop.getValueAsString();
        }
    }
    json["properties"] = props;
    
    // Export children
    const auto& children = node->getChildren();
    if (!children.empty()) {
        nlohmann::json childrenArray = nlohmann::json::array();
        for (const auto& child : children) {
            childrenArray.push_back(exportNodeToJSON(child));
        }
        json["children"] = childrenArray;
    }
    
    return json;
}
#endif

#ifdef HAVE_YAML_CPP
YAML::Node DeviceTree::exportNodeToYAML(std::shared_ptr<DeviceTreeNode> node) const {
    YAML::Node yaml;
    yaml["name"] = node->getName();
    
    // Export properties
    YAML::Node props;
    for (const auto& prop : node->getProperties()) {
        if (prop.isCells()) {
            auto cells = prop.getValueAsCells();
            YAML::Node cellArray;
            for (auto cell : cells) {
                cellArray.push_back(cell);
            }
            props[prop.getName()] = cellArray;
        } else if (prop.isBinary()) {
            auto binary = prop.getValueAsBinary();
            YAML::Node binaryArray;
            for (auto byte : binary) {
                binaryArray.push_back(static_cast<int>(byte));
            }
            props[prop.getName()] = binaryArray;
        } else {
            props[prop.getName()] = prop.getValueAsString();
        }
    }
    yaml["properties"] = props;
    
    // Export children
    const auto& children = node->getChildren();
    if (!children.empty()) {
        YAML::Node childrenArray;
        for (const auto& child : children) {
            childrenArray.push_back(exportNodeToYAML(child));
        }
        yaml["children"] = childrenArray;
    }
    
    return yaml;
}
#endif

void DeviceTree::exportNodeAsJSON(std::shared_ptr<DeviceTreeNode> node, std::ostringstream& json, int indent) const {
    std::string indentStr(indent * 2, ' ');
    
    json << "{\n";
    json << indentStr << "  \"name\": \"" << node->getName() << "\",\n";
    
    // Export properties
    json << indentStr << "  \"properties\": {\n";
    const auto& properties = node->getProperties();
    for (size_t i = 0; i < properties.size(); ++i) {
        json << indentStr << "    \"" << properties[i].getName() << "\": ";
        
        if (properties[i].isString()) {
            json << "\"" << properties[i].getValueAsString() << "\"";
        } else if (properties[i].isCells()) {
            auto cells = properties[i].getValueAsCells();
            json << "[";
            for (size_t j = 0; j < cells.size(); ++j) {
                if (j > 0) json << ", ";
                json << "0x" << std::hex << cells[j];
            }
            json << "]";
        } else if (properties[i].isBinary()) {
            auto binary = properties[i].getValueAsBinary();
            json << "[";
            for (size_t j = 0; j < binary.size(); ++j) {
                if (j > 0) json << ", ";
                json << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(binary[j]);
            }
            json << "]";
        } else {
            json << "\"" << properties[i].getValueAsString() << "\"";
        }
        
        if (i < properties.size() - 1) json << ",";
        json << "\n";
    }
    json << indentStr << "  }";
    
    // Export children
    const auto& children = node->getChildren();
    if (!children.empty()) {
        json << ",\n" << indentStr << "  \"children\": [\n";
        for (size_t i = 0; i < children.size(); ++i) {
            exportNodeAsJSON(children[i], json, indent + 1);
            if (i < children.size() - 1) json << ",";
            json << "\n";
        }
        json << indentStr << "  ]";
    }
    
    json << "\n" << indentStr << "}";
}

void DeviceTree::exportNodeAsYAML(std::shared_ptr<DeviceTreeNode> node, std::ostringstream& yaml, int indent) const {
    std::string indentStr(indent * 2, ' ');
    
    yaml << indentStr << "name: " << node->getName() << "\n";
    
    // Export properties
    const auto& properties = node->getProperties();
    if (!properties.empty()) {
        yaml << indentStr << "properties:\n";
        for (const auto& prop : properties) {
            yaml << indentStr << "  " << prop.getName() << ": ";
            
            if (prop.isString()) {
                yaml << "\"" << prop.getValueAsString() << "\"";
            } else if (prop.isCells()) {
                auto cells = prop.getValueAsCells();
                yaml << "[";
                for (size_t j = 0; j < cells.size(); ++j) {
                    if (j > 0) yaml << ", ";
                    yaml << "0x" << std::hex << cells[j];
                }
                yaml << "]";
            } else if (prop.isBinary()) {
                auto binary = prop.getValueAsBinary();
                yaml << "[";
                for (size_t j = 0; j < binary.size(); ++j) {
                    if (j > 0) yaml << ", ";
                    yaml << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(binary[j]);
                }
                yaml << "]";
            } else {
                yaml << "\"" << prop.getValueAsString() << "\"";
            }
            yaml << "\n";
        }
    }
    
    // Export children
    const auto& children = node->getChildren();
    if (!children.empty()) {
        yaml << indentStr << "children:\n";
        for (const auto& child : children) {
            yaml << indentStr << "  -\n";
            exportNodeAsYAML(child, yaml, indent + 2);
        }
    }
}

} // namespace DTE 