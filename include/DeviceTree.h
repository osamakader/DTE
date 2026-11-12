#ifndef DEVICETREE_H
#define DEVICETREE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <cstdint>

#ifdef HAVE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif

#ifdef HAVE_YAML_CPP
#include <yaml-cpp/yaml.h>
#endif

namespace DTE {

// Property value types
using PropertyValue = std::variant<
    std::string,           // String properties
    std::vector<uint8_t>,  // Binary properties
    std::vector<uint32_t>, // Cell properties (32-bit)
    std::vector<uint64_t>  // Cell properties (64-bit)
>;

// Device tree property
class DeviceTreeProperty {
public:
    DeviceTreeProperty() : name_(""), value_(std::string("")) {}
    DeviceTreeProperty(const std::string& name, const PropertyValue& value);
    
    const std::string& getName() const { return name_; }
    const PropertyValue& getValue() const { return value_; }
    
    // Type checking methods
    bool isString() const;
    bool isBinary() const;
    bool isCells() const;
    bool isCells64() const;
    
    // Value conversion methods
    std::string getValueAsString() const;
    std::vector<uint8_t> getValueAsBinary() const;
    std::vector<uint32_t> getValueAsCells() const;
    std::vector<uint64_t> getValueAsCells64() const;

private:
    std::string name_;
    PropertyValue value_;
};

// Device tree node
class DeviceTreeNode : public std::enable_shared_from_this<DeviceTreeNode> {
public:
    explicit DeviceTreeNode(const std::string& name);
    
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    
    // Parent/child relationships
    std::shared_ptr<DeviceTreeNode> getParent() const { return parent_.lock(); }
    const std::vector<std::shared_ptr<DeviceTreeNode>>& getChildren() const { return children_; }
    void addChild(std::shared_ptr<DeviceTreeNode> child);
    void removeChild(std::shared_ptr<DeviceTreeNode> child);
    
    // Property management
    const std::vector<DeviceTreeProperty>& getProperties() const { return properties_; }
    size_t getPropertyCount() const { return properties_.size(); }
    void addProperty(const DeviceTreeProperty& property);
    void removeProperty(const std::string& name);
    DeviceTreeProperty* findProperty(const std::string& name);
    const DeviceTreeProperty* findProperty(const std::string& name) const;
    
    // Path and search methods
    std::string getFullPath() const;
    std::shared_ptr<DeviceTreeNode> findNodeByPath(const std::string& path) const;
    std::vector<std::shared_ptr<DeviceTreeNode>> findNodesByName(const std::string& name) const;
    std::vector<std::shared_ptr<DeviceTreeNode>> findNodesByPattern(const std::string& pattern) const;

private:
    std::string name_;
    std::weak_ptr<DeviceTreeNode> parent_;
    std::vector<std::shared_ptr<DeviceTreeNode>> children_;
    std::vector<DeviceTreeProperty> properties_;
};

// Entry of the DTB memory reservation block (/memreserve/ in DTS)
struct MemoryReservation {
    uint64_t address;
    uint64_t size;

    bool operator==(const MemoryReservation&) const = default;
};

// Device tree
class DeviceTree {
public:
    DeviceTree();
    
    // Root node management
    std::shared_ptr<DeviceTreeNode> getRoot() const { return root_; }
    void setRoot(std::shared_ptr<DeviceTreeNode> root) { root_ = root; }
    
    // File operations
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename, bool asDTS = false) const;
    
    // Source file information
    const std::string& getSourceFile() const { return sourceFile_; }
    void setSourceFile(const std::string& filename) { sourceFile_ = filename; }

    // DTB header metadata
    const std::vector<MemoryReservation>& getMemoryReservations() const { return memoryReservations_; }
    void setMemoryReservations(std::vector<MemoryReservation> reservations) { memoryReservations_ = std::move(reservations); }
    uint32_t getBootCpuId() const { return bootCpuId_; }
    void setBootCpuId(uint32_t id) { bootCpuId_ = id; }

    // Validation
    bool validate() const;
    std::vector<std::string> getValidationErrors() const;
    
    // Export methods
    std::string exportAsJSON() const;
    std::string exportAsYAML() const;
    
    // Search methods
    std::shared_ptr<DeviceTreeNode> findNodeByPath(const std::string& path) const;
    std::vector<std::shared_ptr<DeviceTreeNode>> findNodesByName(const std::string& name) const;
    std::vector<std::shared_ptr<DeviceTreeNode>> findNodesByPattern(const std::string& pattern) const;

private:
    std::shared_ptr<DeviceTreeNode> root_;
    std::string sourceFile_;
    std::vector<MemoryReservation> memoryReservations_;
    uint32_t bootCpuId_ = 0;
    mutable std::vector<std::string> validationErrors_;
    
    // Export helper methods
    void exportNodeAsJSON(std::shared_ptr<DeviceTreeNode> node, std::ostringstream& json, int indent) const;
    void exportNodeAsYAML(std::shared_ptr<DeviceTreeNode> node, std::ostringstream& yaml, int indent) const;
    
#ifdef HAVE_NLOHMANN_JSON
    nlohmann::json exportNodeToJSON(std::shared_ptr<DeviceTreeNode> node) const;
#endif

#ifdef HAVE_YAML_CPP
    YAML::Node exportNodeToYAML(std::shared_ptr<DeviceTreeNode> node) const;
#endif
};

} // namespace DTE

#endif // DEVICETREE_H 