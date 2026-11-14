#pragma once

#include "DeviceTree.h"
#include <string>
#include <vector>
#include <memory>

namespace DTE {

// Types of changes in device tree
enum class DiffType {
    Added,
    Removed,
    Modified,
    Unchanged
};

// Structure representing a difference
struct DiffEntry {
    DiffType type;
    std::string path;
    std::string propertyName;
    std::string oldValue;
    std::string newValue;
    std::string description;
};

// Device Tree Diff Engine
class DeviceTreeDiff {
public:
    DeviceTreeDiff(std::shared_ptr<DeviceTree> base, std::shared_ptr<DeviceTree> overlay);
    
    // Generate diff between two device trees
    std::vector<DiffEntry> generateDiff() const;
    
    // Get specific types of changes
    std::vector<DiffEntry> getAddedNodes() const;
    std::vector<DiffEntry> getRemovedNodes() const;
    std::vector<DiffEntry> getModifiedProperties() const;
    
    // Summary statistics. Added/removed/modified count every entry of that
    // type (nodes and properties), so they always sum to getTotalChanges().
    // An added or removed node also contributes one entry per property.
    size_t getTotalChanges() const;
    size_t getAddedCount() const;
    size_t getRemovedCount() const;
    size_t getModifiedCount() const;
    
    // Export diff in different formats
    std::string exportAsJSON() const;
    std::string exportAsYAML() const;
    std::string exportAsPatch() const;
    
    // Validation
    bool isValid() const;
    std::vector<std::string> getValidationErrors() const;

private:
    std::shared_ptr<DeviceTree> baseTree_;
    std::shared_ptr<DeviceTree> overlayTree_;
    mutable std::vector<DiffEntry> diffCache_;
    mutable bool diffGenerated_;
    
    // Internal diff generation methods
    void generateDiffInternal() const;
    void compareNodes(const std::shared_ptr<DeviceTreeNode>& baseNode,
                     const std::shared_ptr<DeviceTreeNode>& overlayNode,
                     const std::string& path) const;
    void addWholeNode(const std::shared_ptr<DeviceTreeNode>& node,
                      const std::string& path, DiffType type) const;
    void compareProperties(const std::shared_ptr<DeviceTreeNode>& baseNode,
                          const std::shared_ptr<DeviceTreeNode>& overlayNode,
                          const std::string& path) const;
    
    // Utility methods
    std::string propertyValueToString(const DeviceTreeProperty& prop) const;
    bool propertiesEqual(const DeviceTreeProperty& prop1, const DeviceTreeProperty& prop2) const;
};

// Diff Visualizer for GUI
class DiffVisualizer {
public:
    DiffVisualizer(const DeviceTreeDiff& diff);
    
    // Get formatted diff for display
    std::string getFormattedDiff() const;
    std::string getColoredDiff() const;
    
    // Get diff statistics
    struct DiffStats {
        size_t totalChanges;
        size_t addedNodes;
        size_t removedNodes;
        size_t modifiedProperties;
        size_t addedProperties;
        size_t removedProperties;
    };
    
    DiffStats getStats() const;
    
    // Filter diff entries
    std::vector<DiffEntry> filterByType(DiffType type) const;
    std::vector<DiffEntry> filterByPath(const std::string& pathPattern) const;
    std::vector<DiffEntry> filterByProperty(const std::string& propertyPattern) const;

private:
    const DeviceTreeDiff& diff_;
    mutable DiffStats stats_;
    mutable bool statsCalculated_;
    
    void calculateStats() const;
};

} // namespace DTE 