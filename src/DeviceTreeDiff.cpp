#include "DeviceTreeDiff.h"
#include "Escape.h"
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

namespace {

size_t countType(const std::vector<DiffEntry>& diff, DiffType type) {
    size_t n = 0;
    for (const auto& e : diff) {
        if (e.type == type) ++n;
    }
    return n;
}

std::string childPath(const std::string& parent, const std::string& name) {
    return parent == "/" ? "/" + name : parent + "/" + name;
}

const char* typeName(DiffType type) {
    switch (type) {
    case DiffType::Added: return "added";
    case DiffType::Removed: return "removed";
    case DiffType::Modified: return "modified";
    default: return "unchanged";
    }
}

} // namespace

size_t DeviceTreeDiff::getAddedCount() const {
    return countType(generateDiff(), DiffType::Added);
}

size_t DeviceTreeDiff::getRemovedCount() const {
    return countType(generateDiff(), DiffType::Removed);
}

size_t DeviceTreeDiff::getModifiedCount() const {
    return countType(generateDiff(), DiffType::Modified);
}

std::string DeviceTreeDiff::exportAsJSON() const {
    using detail::jsonQuote;
    const auto& diff = generateDiff();
    
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"diff\": {\n";
    oss << "    \"total_changes\": " << diff.size() << ",\n";
    oss << "    \"added\": " << getAddedCount() << ",\n";
    oss << "    \"removed\": " << getRemovedCount() << ",\n";
    oss << "    \"modified\": " << getModifiedCount() << ",\n";
    oss << "    \"changes\": [";
    
    for (size_t i = 0; i < diff.size(); ++i) {
        const auto& entry = diff[i];
        oss << (i == 0 ? "\n" : ",\n");
        oss << "      {\n";
        oss << "        \"type\": " << jsonQuote(typeName(entry.type)) << ",\n";
        oss << "        \"path\": " << jsonQuote(entry.path) << ",\n";
        if (!entry.propertyName.empty()) {
            oss << "        \"property\": " << jsonQuote(entry.propertyName) << ",\n";
        }
        if (entry.type == DiffType::Modified || entry.type == DiffType::Removed) {
            if (!entry.propertyName.empty()) {
                oss << "        \"old_value\": " << jsonQuote(entry.oldValue) << ",\n";
            }
        }
        if (entry.type == DiffType::Modified || entry.type == DiffType::Added) {
            if (!entry.propertyName.empty()) {
                oss << "        \"new_value\": " << jsonQuote(entry.newValue) << ",\n";
            }
        }
        oss << "        \"description\": " << jsonQuote(entry.description) << "\n";
        oss << "      }";
    }
    
    oss << (diff.empty() ? "]\n" : "\n    ]\n");
    oss << "  }\n";
    oss << "}\n";
    
    return oss.str();
}

std::string DeviceTreeDiff::exportAsYAML() const {
    using detail::jsonQuote;
    const auto& diff = generateDiff();
    
    std::ostringstream oss;
    oss << "diff:\n";
    oss << "  total_changes: " << diff.size() << "\n";
    oss << "  added: " << getAddedCount() << "\n";
    oss << "  removed: " << getRemovedCount() << "\n";
    oss << "  modified: " << getModifiedCount() << "\n";
    oss << "  changes:" << (diff.empty() ? " []\n" : "\n");
    
    for (const auto& entry : diff) {
        oss << "    - type: " << typeName(entry.type) << "\n";
        oss << "      path: " << jsonQuote(entry.path) << "\n";
        if (!entry.propertyName.empty()) {
            oss << "      property: " << jsonQuote(entry.propertyName) << "\n";
            if (entry.type != DiffType::Added) {
                oss << "      old_value: " << jsonQuote(entry.oldValue) << "\n";
            }
            if (entry.type != DiffType::Removed) {
                oss << "      new_value: " << jsonQuote(entry.newValue) << "\n";
            }
        }
        oss << "      description: " << jsonQuote(entry.description) << "\n";
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

void DeviceTreeDiff::addWholeNode(const std::shared_ptr<DeviceTreeNode>& node,
                                  const std::string& path, DiffType type) const {
    const bool added = type == DiffType::Added;

    DiffEntry entry;
    entry.type = type;
    entry.path = path;
    entry.description = (added ? "Node added: " : "Node removed: ") + node->getName();
    diffCache_.push_back(entry);

    for (const auto& prop : node->getProperties()) {
        DiffEntry p;
        p.type = type;
        p.path = path;
        p.propertyName = prop.getName();
        (added ? p.newValue : p.oldValue) = propertyValueToString(prop);
        p.description = (added ? "Property added: " : "Property removed: ") + prop.getName();
        diffCache_.push_back(p);
    }

    for (const auto& child : node->getChildren()) {
        addWholeNode(child, childPath(path, child->getName()), type);
    }
}

void DeviceTreeDiff::compareNodes(const std::shared_ptr<DeviceTreeNode>& baseNode,
                                 const std::shared_ptr<DeviceTreeNode>& overlayNode,
                                 const std::string& path) const {
    if (!baseNode && !overlayNode) {
        return;
    }
    if (!baseNode) {
        addWholeNode(overlayNode, path, DiffType::Added);
        return;
    }
    if (!overlayNode) {
        addWholeNode(baseNode, path, DiffType::Removed);
        return;
    }
    
    compareProperties(baseNode, overlayNode, path);
    
    // Children are matched by name. Output follows the base tree's order,
    // followed by nodes that only exist in the overlay, in overlay order.
    std::map<std::string, std::shared_ptr<DeviceTreeNode>> baseChildren;
    std::map<std::string, std::shared_ptr<DeviceTreeNode>> overlayChildren;
    for (const auto& child : baseNode->getChildren()) {
        baseChildren[child->getName()] = child;
    }
    for (const auto& child : overlayNode->getChildren()) {
        overlayChildren[child->getName()] = child;
    }
    
    for (const auto& child : baseNode->getChildren()) {
        auto it = overlayChildren.find(child->getName());
        compareNodes(child, it == overlayChildren.end() ? nullptr : it->second,
                     childPath(path, child->getName()));
    }
    for (const auto& child : overlayNode->getChildren()) {
        if (!baseChildren.count(child->getName())) {
            compareNodes(nullptr, child, childPath(path, child->getName()));
        }
    }
}

void DeviceTreeDiff::compareProperties(const std::shared_ptr<DeviceTreeNode>& baseNode,
                                      const std::shared_ptr<DeviceTreeNode>& overlayNode,
                                      const std::string& path) const {
    // Same ordering rule as for child nodes.
    for (const auto& baseProp : baseNode->getProperties()) {
        const DeviceTreeProperty* overlayProp =
            static_cast<const DeviceTreeNode&>(*overlayNode).findProperty(baseProp.getName());

        DiffEntry entry;
        entry.path = path;
        entry.propertyName = baseProp.getName();
        if (!overlayProp) {
            entry.type = DiffType::Removed;
            entry.oldValue = propertyValueToString(baseProp);
            entry.description = "Property removed: " + baseProp.getName();
        } else if (!propertiesEqual(baseProp, *overlayProp)) {
            entry.type = DiffType::Modified;
            entry.oldValue = propertyValueToString(baseProp);
            entry.newValue = propertyValueToString(*overlayProp);
            entry.description = "Property modified: " + baseProp.getName();
        } else {
            continue;
        }
        diffCache_.push_back(entry);
    }
    
    for (const auto& overlayProp : overlayNode->getProperties()) {
        if (!static_cast<const DeviceTreeNode&>(*baseNode).findProperty(overlayProp.getName())) {
            DiffEntry entry;
            entry.type = DiffType::Added;
            entry.path = path;
            entry.propertyName = overlayProp.getName();
            entry.newValue = propertyValueToString(overlayProp);
            entry.description = "Property added: " + overlayProp.getName();
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