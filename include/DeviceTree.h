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
    // Zero-length property such as `ranges;` (stored as an empty string)
    bool isEmpty() const;
    bool isBinary() const;
    bool isCells() const;
    bool isCells64() const;
    
    // Value conversion methods
    // getValueAsString() renders the value in DTS syntax: "escaped",
    // "a", "b", <0x1 0x2> or [00 11]; an empty property renders as "" (no
    // text), since `""` in DTS is a one-byte value.
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
    // Inserts at `index` (clamped to the end).
    void insertChild(size_t index, std::shared_ptr<DeviceTreeNode> child);
    // Removes the child and clears its parent link.
    void removeChild(std::shared_ptr<DeviceTreeNode> child);
    // Index of a direct child, or -1.
    int childIndex(const DeviceTreeNode* child) const;
    std::shared_ptr<DeviceTreeNode> findChild(const std::string& name) const;
    // Deep copy of this node and its subtree (the copy has no parent).
    std::shared_ptr<DeviceTreeNode> clone() const;
    
    // Property management
    const std::vector<DeviceTreeProperty>& getProperties() const { return properties_; }
    size_t getPropertyCount() const { return properties_.size(); }
    // Adds the property, or replaces the value of an existing property with
    // the same name in place (as dtc does for duplicate definitions).
    void addProperty(const DeviceTreeProperty& property);
    // Inserts at `index` (clamped); an existing property with the same name
    // is removed first.
    void insertProperty(size_t index, const DeviceTreeProperty& property);
    void removeProperty(const std::string& name);
    // Index of the named property, or -1.
    int propertyIndex(const std::string& name) const;
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

// One phandle reference found in a property, e.g. one entry of
// `clocks = <&clk 3>, <&clk 4>;` (target &clk, args {3}).
struct PhandleReference {
    std::shared_ptr<DeviceTreeNode> source; // node holding the property
    std::string property;
    size_t cellIndex = 0;                   // position of the phandle cell
    uint32_t phandle = 0;
    std::shared_ptr<DeviceTreeNode> target; // nullptr if it does not resolve
    std::vector<uint32_t> args;             // specifier cells after the phandle
    // Set when the entry could not be decoded (unknown phandle, missing
    // #...-cells on the target, truncated list); decoding of the property
    // stops at such an entry.
    std::string problem;
};

// A problem found by DeviceTree::check()
struct ValidationIssue {
    enum class Severity { Error, Warning };
    Severity severity;
    std::string path;      // node path
    std::string property;  // empty for node-level issues
    std::string message;

    bool isError() const { return severity == Severity::Error; }
    // "error: /path:property: message"
    std::string toString() const;
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
    // Compile DTS with `dtc -@`: adds a __symbols__ node (label -> path) and
    // gives every labelled node a phandle. Needed to apply overlays.
    bool symbols = false;
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

    // Validation against the Devicetree Specification's structural rules
    // (names, required root properties, reg vs #address-cells/#size-cells
    // and unit addresses, phandles and phandle references, status,
    // compatible, aliases). Issues are returned in tree order. No dtc
    // needed.
    std::vector<ValidationIssue> check() const;

    // Phandles. Nodes are found through their `phandle` (or legacy
    // `linux,phandle`) property.
    std::shared_ptr<DeviceTreeNode> findNodeByPhandle(uint32_t phandle) const;
    // All references in properties known to hold phandles (interrupt-parent,
    // clocks, gpios / *-gpios, resets, dmas, interrupts-extended, pinctrl-N,
    // ... as in dtc), decoded with the target's #...-cells. Tree order.
    std::vector<PhandleReference> references() const;
    // References whose target is `node`, or (withDescendants) any node in
    // its subtree, made from outside that subtree.
    std::vector<PhandleReference> referencesTo(const std::shared_ptr<DeviceTreeNode>& node,
                                               bool withDescendants = false) const;
    // Decoded references held by one property (empty if it holds none).
    std::vector<PhandleReference> referencesIn(const std::shared_ptr<DeviceTreeNode>& node,
                                               const std::string& property) const;
    // True if check() finds no errors (warnings are allowed).
    bool validate() const;
    
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
};

} // namespace DTE

#endif // DEVICETREE_H 