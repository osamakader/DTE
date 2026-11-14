#include "DeviceTreeDiff.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace DTE {

// DeviceTreeDiff implementation
DeviceTreeDiff::DeviceTreeDiff(std::shared_ptr<DeviceTree> base, std::shared_ptr<DeviceTree> overlay)
    : baseTree_(base), overlayTree_(overlay), diffGenerated_(false) {}

std::vector<DiffEntry> DeviceTreeDiff::generateDiff() const {
    if (!diffGenerated_) {
        generateDiffInternal();
    }
    return diffCache_;
}

std::vector<DiffEntry> DeviceTreeDiff::getAddedNodes() const {
    std::vector<DiffEntry> result;
    auto diff = generateDiff();
    
    for (const auto& entry : diff) {
        if (entry.type == DiffType::Added && entry.propertyName.empty()) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<DiffEntry> DeviceTreeDiff::getRemovedNodes() const {
    std::vector<DiffEntry> result;
    auto diff = generateDiff();
    
    for (const auto& entry : diff) {
        if (entry.type == DiffType::Removed && entry.propertyName.empty()) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<DiffEntry> DeviceTreeDiff::getModifiedProperties() const {
    std::vector<DiffEntry> result;
    auto diff = generateDiff();
    
    for (const auto& entry : diff) {
        if (entry.type == DiffType::Modified && !entry.propertyName.empty()) {
            result.push_back(entry);
        }
    }
    
    return result;
}

size_t DeviceTreeDiff::getTotalChanges() const {
    return generateDiff().size();
}

size_t DeviceTreeDiff::getAddedCount() const {
    return getAddedNodes().size();
}

size_t DeviceTreeDiff::getRemovedCount() const {
    return getRemovedNodes().size();
}

size_t DeviceTreeDiff::getModifiedCount() const {
    return getModifiedProperties().size();
}

std::string DeviceTreeDiff::exportAsJSON() const {
    auto diff = generateDiff();
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"diff\": {\n";
    oss << "    \"total_changes\": " << diff.size() << ",\n";
    oss << "    \"added\": " << getAddedCount() << ",\n";
    oss << "    \"removed\": " << getRemovedCount() << ",\n";
    oss << "    \"modified\": " << getModifiedCount() << ",\n";
    oss << "    \"changes\": [\n";
    
    for (size_t i = 0; i < diff.size(); ++i) {
        const auto& entry = diff[i];
        oss << "      {\n";
        oss << "        \"type\": \"";
        switch (entry.type) {
            case DiffType::Added: oss << "added"; break;
            case DiffType::Removed: oss << "removed"; break;
            case DiffType::Modified: oss << "modified"; break;
            default: oss << "unchanged"; break;
        }
        oss << "\",\n";
        oss << "        \"path\": \"" << entry.path << "\",\n";
        if (!entry.propertyName.empty()) {
            oss << "        \"property\": \"" << entry.propertyName << "\",\n";
        }
        if (!entry.oldValue.empty()) {
            oss << "        \"old_value\": \"" << entry.oldValue << "\",\n";
        }
        if (!entry.newValue.empty()) {
            oss << "        \"new_value\": \"" << entry.newValue << "\",\n";
        }
        oss << "        \"description\": \"" << entry.description << "\"\n";
        oss << "      }";
        if (i < diff.size() - 1) oss << ",";
        oss << "\n";
    }
    
    oss << "    ]\n";
    oss << "  }\n";
    oss << "}\n";
    
    return oss.str();
}

std::string DeviceTreeDiff::exportAsYAML() const {
    auto diff = generateDiff();
    
    std::ostringstream oss;
    oss << "diff:\n";
    oss << "  total_changes: " << diff.size() << "\n";
    oss << "  added: " << getAddedCount() << "\n";
    oss << "  removed: " << getRemovedCount() << "\n";
    oss << "  modified: " << getModifiedCount() << "\n";
    oss << "  changes:\n";
    
    for (const auto& entry : diff) {
        oss << "    - type: ";
        switch (entry.type) {
            case DiffType::Added: oss << "added"; break;
            case DiffType::Removed: oss << "removed"; break;
            case DiffType::Modified: oss << "modified"; break;
            default: oss << "unchanged"; break;
        }
        oss << "\n";
        oss << "      path: " << entry.path << "\n";
        if (!entry.propertyName.empty()) {
            oss << "      property: " << entry.propertyName << "\n";
        }
        if (!entry.oldValue.empty()) {
            oss << "      old_value: " << entry.oldValue << "\n";
        }
        if (!entry.newValue.empty()) {
            oss << "      new_value: " << entry.newValue << "\n";
        }
        oss << "      description: " << entry.description << "\n";
    }
    
    return oss.str();
}

std::string DeviceTreeDiff::exportAsPatch() const {
    auto diff = generateDiff();
    
    std::ostringstream oss;
    oss << "--- Device Tree Diff ---\n";
    oss << "Total changes: " << diff.size() << "\n";
    oss << "Added: " << getAddedCount() << ", Removed: " << getRemovedCount() 
        << ", Modified: " << getModifiedCount() << "\n\n";
    
    for (const auto& entry : diff) {
        oss << "[" << (entry.type == DiffType::Added ? "+" : 
                      entry.type == DiffType::Removed ? "-" : "~") << "] ";
        oss << entry.path;
        if (!entry.propertyName.empty()) {
            oss << ":" << entry.propertyName;
        }
        oss << "\n";
        
        if (entry.type == DiffType::Modified) {
            oss << "  - " << entry.oldValue << "\n";
            oss << "  + " << entry.newValue << "\n";
        } else if (entry.type == DiffType::Added) {
            oss << "  + " << entry.newValue << "\n";
        } else if (entry.type == DiffType::Removed) {
            oss << "  - " << entry.oldValue << "\n";
        }
        oss << "\n";
    }
    
    return oss.str();
}

bool DeviceTreeDiff::isValid() const {
    return baseTree_ && overlayTree_;
}

std::vector<std::string> DeviceTreeDiff::getValidationErrors() const {
    std::vector<std::string> errors;
    
    if (!baseTree_) {
        errors.push_back("Base device tree is null");
    }
    
    if (!overlayTree_) {
        errors.push_back("Overlay device tree is null");
    }
    
    return errors;
}

void DeviceTreeDiff::generateDiffInternal() const {
    diffCache_.clear();
    
    if (!isValid()) {
        return;
    }
    
    // Compare root nodes
    compareNodes(baseTree_->getRoot(), overlayTree_->getRoot(), "/");
    diffGenerated_ = true;
}

void DeviceTreeDiff::compareNodes(const std::shared_ptr<DeviceTreeNode>& baseNode,
                                 const std::shared_ptr<DeviceTreeNode>& overlayNode,
                                 const std::string& path) const {
    if (!baseNode && !overlayNode) {
        return;
    }
    
    if (!baseNode && overlayNode) {
        // Node added in overlay
        DiffEntry entry;
        entry.type = DiffType::Added;
        entry.path = path;
        entry.description = "Node added: " + overlayNode->getName();
        diffCache_.push_back(entry);
        
        // Recursively add all child nodes
        for (const auto& child : overlayNode->getChildren()) {
            std::string childPath = path + "/" + child->getName();
            compareNodes(nullptr, child, childPath);
        }
        return;
    }
    
    if (baseNode && !overlayNode) {
        // Node removed in overlay
        DiffEntry entry;
        entry.type = DiffType::Removed;
        entry.path = path;
        entry.description = "Node removed: " + baseNode->getName();
        diffCache_.push_back(entry);
        
        // Recursively remove all child nodes
        for (const auto& child : baseNode->getChildren()) {
            std::string childPath = path + "/" + child->getName();
            compareNodes(child, nullptr, childPath);
        }
        return;
    }
    
    // Both nodes exist, compare properties
    compareProperties(baseNode, overlayNode, path);
    
    // Compare child nodes
    std::map<std::string, std::shared_ptr<DeviceTreeNode>> baseChildren;
    std::map<std::string, std::shared_ptr<DeviceTreeNode>> overlayChildren;
    
    for (const auto& child : baseNode->getChildren()) {
        baseChildren[child->getName()] = child;
    }
    
    for (const auto& child : overlayNode->getChildren()) {
        overlayChildren[child->getName()] = child;
    }
    
    // Find added, removed, and common children
    for (const auto& pair : overlayChildren) {
        auto it = baseChildren.find(pair.first);
        if (it == baseChildren.end()) {
            // Child added
            std::string childPath = path + "/" + pair.first;
            compareNodes(nullptr, pair.second, childPath);
        } else {
            // Child exists in both, compare recursively
            std::string childPath = path + "/" + pair.first;
            compareNodes(it->second, pair.second, childPath);
        }
    }
    
    for (const auto& pair : baseChildren) {
        if (overlayChildren.find(pair.first) == overlayChildren.end()) {
            // Child removed
            std::string childPath = path + "/" + pair.first;
            compareNodes(pair.second, nullptr, childPath);
        }
    }
}

void DeviceTreeDiff::compareProperties(const std::shared_ptr<DeviceTreeNode>& baseNode,
                                      const std::shared_ptr<DeviceTreeNode>& overlayNode,
                                      const std::string& path) const {
    auto baseProps = baseNode->getProperties();
    auto overlayProps = overlayNode->getProperties();
    
    std::map<std::string, DeviceTreeProperty> basePropMap;
    std::map<std::string, DeviceTreeProperty> overlayPropMap;
    
    for (const auto& prop : baseProps) {
        basePropMap[prop.getName()] = prop;
    }
    
    for (const auto& prop : overlayProps) {
        overlayPropMap[prop.getName()] = prop;
    }
    
    // Find added, removed, and modified properties
    for (const auto& pair : overlayPropMap) {
        auto it = basePropMap.find(pair.first);
        if (it == basePropMap.end()) {
            // Property added
            DiffEntry entry;
            entry.type = DiffType::Added;
            entry.path = path;
            entry.propertyName = pair.first;
            entry.newValue = propertyValueToString(pair.second);
            entry.description = "Property added: " + pair.first;
            diffCache_.push_back(entry);
        } else {
            // Property exists in both, check if modified
            if (!propertiesEqual(it->second, pair.second)) {
                DiffEntry entry;
                entry.type = DiffType::Modified;
                entry.path = path;
                entry.propertyName = pair.first;
                entry.oldValue = propertyValueToString(it->second);
                entry.newValue = propertyValueToString(pair.second);
                entry.description = "Property modified: " + pair.first;
                diffCache_.push_back(entry);
            }
        }
    }
    
    for (const auto& pair : basePropMap) {
        if (overlayPropMap.find(pair.first) == overlayPropMap.end()) {
            // Property removed
            DiffEntry entry;
            entry.type = DiffType::Removed;
            entry.path = path;
            entry.propertyName = pair.first;
            entry.oldValue = propertyValueToString(pair.second);
            entry.description = "Property removed: " + pair.first;
            diffCache_.push_back(entry);
        }
    }
}

std::string DeviceTreeDiff::propertyValueToString(const DeviceTreeProperty& prop) const {
    return prop.getValueAsString();
}

bool DeviceTreeDiff::propertiesEqual(const DeviceTreeProperty& prop1, const DeviceTreeProperty& prop2) const {
    return prop1.getValue() == prop2.getValue();
}

// DiffVisualizer implementation
DiffVisualizer::DiffVisualizer(const DeviceTreeDiff& diff)
    : diff_(diff), statsCalculated_(false) {}

std::string DiffVisualizer::getFormattedDiff() const {
    auto diff = diff_.generateDiff();
    
    std::ostringstream oss;
    oss << "Device Tree Diff Report\n";
    oss << "=======================\n\n";
    
    auto stats = getStats();
    oss << "Summary:\n";
    oss << "  Total changes: " << stats.totalChanges << "\n";
    oss << "  Added nodes: " << stats.addedNodes << "\n";
    oss << "  Removed nodes: " << stats.removedNodes << "\n";
    oss << "  Modified properties: " << stats.modifiedProperties << "\n";
    oss << "  Added properties: " << stats.addedProperties << "\n";
    oss << "  Removed properties: " << stats.removedProperties << "\n\n";
    
    oss << "Detailed Changes:\n";
    oss << "=================\n\n";
    
    for (const auto& entry : diff) {
        std::string typeStr;
        switch (entry.type) {
            case DiffType::Added: typeStr = "[ADD]"; break;
            case DiffType::Removed: typeStr = "[DEL]"; break;
            case DiffType::Modified: typeStr = "[MOD]"; break;
            default: typeStr = "[UNK]"; break;
        }
        
        oss << typeStr << " " << entry.path;
        if (!entry.propertyName.empty()) {
            oss << ":" << entry.propertyName;
        }
        oss << "\n";
        oss << "    " << entry.description << "\n";
        
        if (entry.type == DiffType::Modified) {
            oss << "    Old: " << entry.oldValue << "\n";
            oss << "    New: " << entry.newValue << "\n";
        } else if (entry.type == DiffType::Added) {
            oss << "    Value: " << entry.newValue << "\n";
        } else if (entry.type == DiffType::Removed) {
            oss << "    Value: " << entry.oldValue << "\n";
        }
        oss << "\n";
    }
    
    return oss.str();
}

std::string DiffVisualizer::getColoredDiff() const {
    // Similar to getFormattedDiff but with ANSI color codes
    auto diff = diff_.generateDiff();
    
    std::ostringstream oss;
    oss << "\033[1mDevice Tree Diff Report\033[0m\n";
    oss << "=======================\n\n";
    
    auto stats = getStats();
    oss << "\033[1mSummary:\033[0m\n";
    oss << "  Total changes: " << stats.totalChanges << "\n";
    oss << "  Added nodes: " << stats.addedNodes << "\n";
    oss << "  Removed nodes: " << stats.removedNodes << "\n";
    oss << "  Modified properties: " << stats.modifiedProperties << "\n";
    oss << "  Added properties: " << stats.addedProperties << "\n";
    oss << "  Removed properties: " << stats.removedProperties << "\n\n";
    
    oss << "\033[1mDetailed Changes:\033[0m\n";
    oss << "=================\n\n";
    
    for (const auto& entry : diff) {
        std::string typeStr;
        std::string colorCode;
        
        switch (entry.type) {
            case DiffType::Added: 
                typeStr = "[ADD]"; 
                colorCode = "\033[32m"; // Green
                break;
            case DiffType::Removed: 
                typeStr = "[DEL]"; 
                colorCode = "\033[31m"; // Red
                break;
            case DiffType::Modified: 
                typeStr = "[MOD]"; 
                colorCode = "\033[33m"; // Yellow
                break;
            default: 
                typeStr = "[UNK]"; 
                colorCode = "\033[0m"; // Reset
                break;
        }
        
        oss << colorCode << typeStr << "\033[0m " << entry.path;
        if (!entry.propertyName.empty()) {
            oss << ":" << entry.propertyName;
        }
        oss << "\n";
        oss << "    " << entry.description << "\n";
        
        if (entry.type == DiffType::Modified) {
            oss << "\033[31m    Old: " << entry.oldValue << "\033[0m\n";
            oss << "\033[32m    New: " << entry.newValue << "\033[0m\n";
        } else if (entry.type == DiffType::Added) {
            oss << "\033[32m    Value: " << entry.newValue << "\033[0m\n";
        } else if (entry.type == DiffType::Removed) {
            oss << "\033[31m    Value: " << entry.oldValue << "\033[0m\n";
        }
        oss << "\n";
    }
    
    return oss.str();
}

DiffVisualizer::DiffStats DiffVisualizer::getStats() const {
    if (!statsCalculated_) {
        calculateStats();
    }
    return stats_;
}

std::vector<DiffEntry> DiffVisualizer::filterByType(DiffType type) const {
    std::vector<DiffEntry> result;
    auto diff = diff_.generateDiff();
    
    for (const auto& entry : diff) {
        if (entry.type == type) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<DiffEntry> DiffVisualizer::filterByPath(const std::string& pathPattern) const {
    std::vector<DiffEntry> result;
    auto diff = diff_.generateDiff();
    
    for (const auto& entry : diff) {
        if (entry.path.find(pathPattern) != std::string::npos) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<DiffEntry> DiffVisualizer::filterByProperty(const std::string& propertyPattern) const {
    std::vector<DiffEntry> result;
    auto diff = diff_.generateDiff();
    
    for (const auto& entry : diff) {
        if (entry.propertyName.find(propertyPattern) != std::string::npos) {
            result.push_back(entry);
        }
    }
    
    return result;
}

void DiffVisualizer::calculateStats() const {
    auto diff = diff_.generateDiff();
    
    stats_.totalChanges = diff.size();
    stats_.addedNodes = 0;
    stats_.removedNodes = 0;
    stats_.modifiedProperties = 0;
    stats_.addedProperties = 0;
    stats_.removedProperties = 0;
    
    for (const auto& entry : diff) {
        if (entry.propertyName.empty()) {
            // Node-level change
            if (entry.type == DiffType::Added) {
                stats_.addedNodes++;
            } else if (entry.type == DiffType::Removed) {
                stats_.removedNodes++;
            }
        } else {
            // Property-level change
            if (entry.type == DiffType::Added) {
                stats_.addedProperties++;
            } else if (entry.type == DiffType::Removed) {
                stats_.removedProperties++;
            } else if (entry.type == DiffType::Modified) {
                stats_.modifiedProperties++;
            }
        }
    }
    
    statsCalculated_ = true;
}

} // namespace DTE 