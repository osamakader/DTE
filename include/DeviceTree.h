#ifndef DEVICETREE_H
#define DEVICETREE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <cstdint>

namespace DTE {

// List of strings, e.g. compatible = "vendor,a", "vendor,b";
using StringList = std::vector<std::string>;

// Property value types
using PropertyValue = std::variant<
    std::string,           // Single string (also used for empty properties)
    std::vector<uint8_t>,  // Binary properties
    std::vector<uint32_t>, // Cell properties (32-bit)
    std::vector<uint64_t>, // Cell properties (64-bit)
    StringList             // Two or more strings
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
    bool isStringList() const;
    bool isBinary() const;
    bool isCells() const;
    bool isCells64() const;
    
    // Value conversion methods
    // getValueAsString() renders the value in DTS syntax: "escaped",
    // "a", "b", <0x1 0x2> or [00 11].
    std::string getValueAsString() const;
    // The string(s) of a string or string-list property, unescaped;
    // empty for other types.
    StringList getValueAsStrings() const;
    std::vector<uint8_t> getValueAsBinary() const;
    std::vector<uint32_t> getValueAsCells() const;
    std::vector<uint64_t> getValueAsCells64() const;

    bool operator==(const DeviceTreeProperty& other) const = default;

private:
    std::string name_;
    PropertyValue value_;
};

// Quotes and escapes a string for DTS output (\\, \", \n, \t, \xNN).
std::string quoteDtsString(const std::string& s);

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

// Options for DeviceTree::search
struct SearchOptions {
    bool properties = false;     // also match property names and values
    bool caseSensitive = false;
};

// One search hit: a node matched by name/path (propertyName empty), or one
// of its properties matched by name or value.
struct SearchMatch {
    std::shared_ptr<DeviceTreeNode> node;
    std::string propertyName;
};

// Options for DeviceTree::loadFromFile
struct LoadOptions {
    // Extra include directories for DTS sources (#include and /include/)
    std::vector<std::string> includePaths;
};

// Device tree
class DeviceTree {
public:
    DeviceTree();
    
    // Root node management
    std::shared_ptr<DeviceTreeNode> getRoot() const { return root_; }
    void setRoot(std::shared_ptr<DeviceTreeNode> root) { root_ = root; }
    
    // File operations
    // Loads a DTB or DTS file. On failure returns false, leaves the tree
    // unchanged and sets getLoadError().
    bool loadFromFile(const std::string& filename, const LoadOptions& options = {});
    const std::string& getLoadError() const { return loadError_; }
    // Writes the tree as DTS source (asDTS) or as a DTB blob. Returns false
    // on failure and, if `error` is given, stores the reason there.
    bool saveToFile(const std::string& filename, bool asDTS = false,
                    std::string* error = nullptr) const;
    
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
    
    // DTS source accepted by dtc, and an FDT v17 blob. Both work without
    // dtc. Note that a DTB does not record value types, so reading it back
    // re-infers them (e.g. 64-bit cells come back as 32-bit cell pairs).
    std::string exportAsDTS() const;
    std::vector<uint8_t> exportAsDTB() const;

    // Export methods (built-in writers; see DeviceTree.cpp for the schema)
    std::string exportAsJSON() const;
    std::string exportAsYAML() const;
    
    // Search methods
    std::shared_ptr<DeviceTreeNode> findNodeByPath(const std::string& path) const;
    std::vector<std::shared_ptr<DeviceTreeNode>> findNodesByName(const std::string& name) const;
    std::vector<std::shared_ptr<DeviceTreeNode>> findNodesByPattern(const std::string& pattern) const;

    // Substring search in tree order. A node matches on its name, or on its
    // full path when the query contains '/'. With options.properties, each
    // property whose name or DTS-rendered value matches is a separate hit.
    std::vector<SearchMatch> search(const std::string& query, const SearchOptions& options = {}) const;

private:
    std::shared_ptr<DeviceTreeNode> root_;
    std::string sourceFile_;
    std::string loadError_;
    std::vector<MemoryReservation> memoryReservations_;
    uint32_t bootCpuId_ = 0;
    mutable std::vector<std::string> validationErrors_;
};

} // namespace DTE

#endif // DEVICETREE_H 